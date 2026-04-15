// SPDX-FileCopyrightText: Copyright (c) Ken Martin, Will Schroeder, Bill Lorensen
// SPDX-License-Identifier: BSD-3-Clause
// This file reads the Fluent Common Fluid Format. It uses the HDF5 library
// Original author : Arthur Piquet
//
// This class is based on the vtkFLUENTReader class from Brian W. Dotson &
// Terry E. Jordan (Department of Energy, National Energy Technology
// Laboratory) & Douglas McCorkle (Iowa State University)
//
// This class could be improved for memory performance but the developer
// will need to rewrite entirely the structure of the class.

#include "vtkFLUENTCFFReader.h"

#include "vtkCellArray.h"
#include "vtkCellData.h"
#include "vtkDataArraySelection.h"
#include "vtkDoubleArray.h"
#include "vtkFLUENTCFFInternal.h"
#include "vtkFieldData.h"
#include "vtkHexahedron.h"
#include "vtkIdList.h"
#include "vtkInformation.h"
#include "vtkInformationVector.h"
#include "vtkIntArray.h"
#include "vtkIdTypeArray.h"
#include "vtkMultiBlockDataSet.h"
#include "vtkObjectFactory.h"
#include "vtkPolyData.h"
#include "vtkPolygon.h"
#include "vtkPoints.h"
#include "vtkPyramid.h"
#include "vtkQuad.h"
#include "vtkTetra.h"
#include "vtkTriangle.h"
#include "vtkUnstructuredGrid.h"
#include "vtkWedge.h"

#include "vtk_hdf5.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string>
#include <sys/stat.h>
#include <unordered_map>

#if !defined(NDEBUG)
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#endif
#include <utility>
#include <vector>

#define CHECK_HDF(fct)                                                                             \
  if (fct < 0)                                                                                     \
  throw std::runtime_error("HDF5 error in vtkFLUENTCFFReader: " + std::string(__func__) + " at " + \
    std::to_string(__LINE__))

VTK_ABI_NAMESPACE_BEGIN
vtkStandardNewMacro(vtkFLUENTCFFReader);

namespace
{
std::vector<std::string> SplitFieldNames(const std::string& fieldList)
{
  std::vector<std::string> result;
  std::size_t npos = 0;
  while (npos < fieldList.length())
  {
    const std::size_t next = fieldList.find(';', npos);
    if (next == std::string::npos)
    {
      if (npos < fieldList.length())
      {
        result.push_back(fieldList.substr(npos));
      }
      break;
    }
    if (next > npos)
    {
      result.push_back(fieldList.substr(npos, next - npos));
    }
    npos = next + 1;
  }
  return result;
}

#if !defined(NDEBUG)
std::string FluentCffDebugTimestamp()
{
  const auto now = std::chrono::system_clock::now();
  const auto tt = std::chrono::system_clock::to_time_t(now);
  std::tm localTime{};
#if defined(_WIN32)
  localtime_s(&localTime, &tt);
#else
  localtime_r(&tt, &localTime);
#endif
  const auto ms =
    std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;

  std::ostringstream oss;
  oss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S") << "." << std::setw(3) << std::setfill('0')
      << ms;
  return oss.str();
}

void FluentCffDebugLog(const std::string& message)
{
  static std::mutex logMutex;
  static std::ofstream logFile;
  static bool initialized = false;
  std::lock_guard<std::mutex> lock(logMutex);

  if (!initialized)
  {
    if (const char* logPath = std::getenv("FLUENT_CFF_DEBUG_LOG"))
    {
      if (logPath[0] != '\0')
      {
        logFile.open(logPath, std::ios::app);
      }
    }
    initialized = true;
  }

  const std::string line =
    "[" + FluentCffDebugTimestamp() + "][vtkFLUENTCFFReader][debug] " + message;
  std::cerr << line << '\n';
  if (logFile.is_open())
  {
    logFile << line << '\n';
    logFile.flush();
  }
}

void FluentCffDebugPrintMs(const char* label, std::chrono::steady_clock::time_point t0)
{
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::steady_clock::now() - t0)
                    .count();
  FluentCffDebugLog(std::string(label) + " " + std::to_string(ms) + " ms");
}
#endif
}

//------------------------------------------------------------------------------
struct vtkFLUENTCFFReader::vtkInternals
{
  hid_t FluentCaseFile;
  hid_t FluentDataFile;
};

//------------------------------------------------------------------------------
vtkFLUENTCFFReader::vtkFLUENTCFFReader()
  : HDFImpl(new vtkFLUENTCFFReader::vtkInternals)
{
  this->HDFImpl->FluentCaseFile = -1;
  this->HDFImpl->FluentDataFile = -1;
  H5Eset_auto(H5E_DEFAULT, nullptr, nullptr);
  this->SetNumberOfInputPorts(0);
}

//------------------------------------------------------------------------------
vtkFLUENTCFFReader::~vtkFLUENTCFFReader()
{
  if (this->HDFImpl->FluentCaseFile >= 0)
  {
    H5Fclose(this->HDFImpl->FluentCaseFile);
  }
  if (this->HDFImpl->FluentDataFile >= 0)
  {
    H5Fclose(this->HDFImpl->FluentDataFile);
  }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::ResetMeshState()
{
  this->Points->Reset();
  this->Cells.clear();
  this->Faces.clear();
  this->FaceNodePool.clear();
  this->CellNodePool.clear();
  this->CellNodeOffsetPool.clear();
  this->CellUniqueNodePool.clear();
  this->CellZones.clear();
  this->FaceZones.clear();
  this->CellIndicesByZone.clear();
  this->FaceZoneTopologyCaches.clear();
  this->NumberOfCells = 0;
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::ClearData()
{
  this->CellDataChunks.clear();
  this->FaceDataChunks.clear();
  this->PreReadCellData.clear();
  this->PreReadFaceData.clear();
  this->NumberOfArrays = 0;
  this->CellDataArraySelection->RemoveAllArrays();
  this->FaceDataArraySelection->RemoveAllArrays();
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::RequestData(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** vtkNotUsed(inputVector), vtkInformationVector* outputVector)
{
  if (this->FileName.empty())
  {
    vtkErrorMacro("FileName has to be specified!");
    return 0;
  }

  if (this->HDFImpl->FluentCaseFile < 0)
  {
    vtkErrorMacro("HDF5 file not opened!");
    return 0;
  }

  vtkInformation* outInfo = outputVector->GetInformationObject(0);

  vtkMultiBlockDataSet* output =
    vtkMultiBlockDataSet::SafeDownCast(outInfo->Get(vtkMultiBlockDataSet::DATA_OBJECT()));

  this->ResetMeshState();

  // Read data (Fluent Format)
  int parseFlag = this->ParseCaseFile();
  if (parseFlag == 0)
  {
    vtkErrorMacro("Unable to read the Case CFF file. The structure of the file may have changed.");
    return 0;
  }

  try
  {
    this->CleanCells();
    this->PopulateCellNodes();
    this->GetNumberOfCellZones();
  }
  catch (std::runtime_error const& e)
  {
    vtkErrorMacro(<< e.what());
    return 0;
  }

  this->NumberOfArrays = 0;
  if (this->FileState == DataState::AVAILABLE)
  {
    int flagData = 0;
    try
    {
      flagData = this->GetData();
    }
    catch (std::runtime_error const& e)
    {
      vtkErrorMacro(<< e.what());
      return 0;
    }
    if (flagData == 0)
    {
      vtkErrorMacro(
        "Unable to read the Data CFF file. The structure of the file may have changed.");
      return 0;
    }
    this->PopulateCellTree();
    this->FileState = DataState::LOADED;
  }

  // Transfer structures for VTK polyhedron cells
  vtkNew<vtkCellArray> faces;
  // Convert Fluent format to VTK
  this->NumberOfCells = static_cast<vtkIdType>(this->Cells.size());

  output->SetNumberOfBlocks(static_cast<unsigned int>(this->CellZones.size()));

  std::vector<vtkSmartPointer<vtkUnstructuredGrid>> grid(
    this->CellZones.size(), vtkSmartPointer<vtkUnstructuredGrid>::New());

  for (auto& g : grid)
  {
    g = vtkUnstructuredGrid::New();
  }

  std::unordered_map<int, std::size_t> zoneToLocation;
  zoneToLocation.reserve(this->CellZones.size());
  this->CellIndicesByZone.clear();
  this->CellIndicesByZone.resize(this->CellZones.size());
  std::vector<std::size_t> cellLocationByIndex(
    this->Cells.size(), std::numeric_limits<std::size_t>::max());
  for (std::size_t location = 0; location < this->CellZones.size(); ++location)
  {
    zoneToLocation.emplace(this->CellZones[location], location);
  }
  for (vtkIdType cellId = 0; cellId < static_cast<vtkIdType>(this->Cells.size()); ++cellId)
  {
    const auto zoneIt = zoneToLocation.find(this->Cells[static_cast<std::size_t>(cellId)].zone);
    if (zoneIt != zoneToLocation.end())
    {
      this->CellIndicesByZone[zoneIt->second].push_back(cellId);
      cellLocationByIndex[static_cast<std::size_t>(cellId)] = zoneIt->second;
    }
  }

#if !defined(NDEBUG)
  std::chrono::steady_clock::duration polyhedronWriteDuration{};
  std::chrono::steady_clock::duration polyhedronBufferPrepDuration{};
  std::chrono::steady_clock::duration polyhedronNodeCopyDuration{};
  std::chrono::steady_clock::duration polyhedronOffsetCopyDuration{};
  std::chrono::steady_clock::duration polyhedronUniqueSelectDuration{};
  std::chrono::steady_clock::duration polyhedronSetDataDuration{};
  vtkIdType polyhedronCellCount = 0;
#endif
  vtkNew<vtkIdTypeArray> polyNodes;
  vtkNew<vtkIdTypeArray> polyNodeOffsets;
  polyNodes->SetNumberOfComponents(1);
  polyNodeOffsets->SetNumberOfComponents(1);
  vtkIdType polyNodeCapacity = 0;
  vtkIdType polyNodeOffsetCapacity = 0;
#if !defined(NDEBUG)
  const auto polySetData0 = std::chrono::steady_clock::now();
#endif
  faces->SetData(polyNodeOffsets, polyNodes);
#if !defined(NDEBUG)
  polyhedronSetDataDuration += (std::chrono::steady_clock::now() - polySetData0);
#endif
  std::vector<vtkIdType> uniquePointIds;
  std::vector<int> pointVisitStamp;
  pointVisitStamp.resize(static_cast<std::size_t>(this->Points->GetNumberOfPoints()), -1);
  int visitToken = 0;

  for (vtkIdType cellId = 0; cellId < static_cast<vtkIdType>(this->Cells.size()); ++cellId)
  {
    const std::size_t location = cellLocationByIndex[static_cast<std::size_t>(cellId)];
    if (location == std::numeric_limits<std::size_t>::max())
    {
      continue;
    }
    const auto& cell = this->Cells[static_cast<std::size_t>(cellId)];
    const int* cellNodes = nullptr;
    std::size_t cellNodeCount = 0;
    if (cell.nodePoolCount > 0)
    {
      const std::size_t nodeOffset = static_cast<std::size_t>(cell.nodePoolOffset);
      if (nodeOffset + static_cast<std::size_t>(cell.nodePoolCount) <= this->CellNodePool.size())
      {
        cellNodes = this->CellNodePool.data() + nodeOffset;
        cellNodeCount = static_cast<std::size_t>(cell.nodePoolCount);
      }
    }
    if (!cellNodes)
    {
      cellNodes = cell.nodes.data();
      cellNodeCount = cell.nodes.size();
    }

    if (cell.type == 1)
    {
      for (int j = 0; j < 3; j++)
      {
        this->Triangle->GetPointIds()->SetId(j, cellNodes[j]);
      }
      grid[location]->InsertNextCell(this->Triangle->GetCellType(), this->Triangle->GetPointIds());
    }
    else if (cell.type == 2)
    {
      for (int j = 0; j < 4; j++)
      {
        this->Tetra->GetPointIds()->SetId(j, cellNodes[j]);
      }
      grid[location]->InsertNextCell(this->Tetra->GetCellType(), this->Tetra->GetPointIds());
    }
    else if (cell.type == 3)
    {
      for (int j = 0; j < 4; j++)
      {
        this->Quad->GetPointIds()->SetId(j, cellNodes[j]);
      }
      grid[location]->InsertNextCell(this->Quad->GetCellType(), this->Quad->GetPointIds());
    }
    else if (cell.type == 4)
    {
      for (int j = 0; j < 8; j++)
      {
        this->Hexahedron->GetPointIds()->SetId(j, cellNodes[j]);
      }
      grid[location]->InsertNextCell(
        this->Hexahedron->GetCellType(), this->Hexahedron->GetPointIds());
    }
    else if (cell.type == 5)
    {
      for (int j = 0; j < 5; j++)
      {
        this->Pyramid->GetPointIds()->SetId(j, cellNodes[j]);
      }
      grid[location]->InsertNextCell(this->Pyramid->GetCellType(), this->Pyramid->GetPointIds());
    }
    else if (cell.type == 6)
    {
      for (int j = 0; j < 6; j++)
      {
        this->Wedge->GetPointIds()->SetId(j, cellNodes[j]);
      }
      grid[location]->InsertNextCell(this->Wedge->GetCellType(), this->Wedge->GetPointIds());
    }
    else if (cell.type == 7)
    {
#if !defined(NDEBUG)
      const auto polyhedronStep0 = std::chrono::steady_clock::now();
#endif
      const int* cellNodeOffsets = nullptr;
      std::size_t cellNodeOffsetCount = 0;
      if (cell.nodeOffsetPoolCount > 0)
      {
        cellNodeOffsets =
          this->CellNodeOffsetPool.data() + static_cast<std::size_t>(cell.nodeOffsetPoolOffset);
        cellNodeOffsetCount = static_cast<std::size_t>(cell.nodeOffsetPoolCount);
      }
      else
      {
        cellNodeOffsets = cell.nodesOffset.data();
        cellNodeOffsetCount = cell.nodesOffset.size();
      }
#if !defined(NDEBUG)
      const auto polyBuffer0 = std::chrono::steady_clock::now();
#endif
      const vtkIdType nodeTupleCount = static_cast<vtkIdType>(cellNodeCount);
      if (nodeTupleCount > polyNodeCapacity)
      {
        polyNodes->WritePointer(0, nodeTupleCount);
        polyNodeCapacity = nodeTupleCount;
      }
      else
      {
        polyNodes->SetNumberOfValues(nodeTupleCount);
      }
      vtkIdType* nodePtr = polyNodes->GetPointer(0);
#if !defined(NDEBUG)
      const auto polyNodeCopy0 = std::chrono::steady_clock::now();
#endif
      for (std::size_t j = 0; j < cellNodeCount; ++j)
      {
        nodePtr[j] = static_cast<vtkIdType>(cellNodes[j]);
      }
#if !defined(NDEBUG)
      polyhedronNodeCopyDuration += (std::chrono::steady_clock::now() - polyNodeCopy0);
#endif
      const vtkIdType nodeOffsetTupleCount = static_cast<vtkIdType>(cellNodeOffsetCount);
      if (nodeOffsetTupleCount > polyNodeOffsetCapacity)
      {
        polyNodeOffsets->WritePointer(0, nodeOffsetTupleCount);
        polyNodeOffsetCapacity = nodeOffsetTupleCount;
      }
      else
      {
        polyNodeOffsets->SetNumberOfValues(nodeOffsetTupleCount);
      }
      vtkIdType* nodeOffsetPtr = polyNodeOffsets->GetPointer(0);
#if !defined(NDEBUG)
      const auto polyOffsetCopy0 = std::chrono::steady_clock::now();
#endif
      for (std::size_t j = 0; j < cellNodeOffsetCount; ++j)
      {
        nodeOffsetPtr[j] = static_cast<vtkIdType>(cellNodeOffsets[j]);
      }
#if !defined(NDEBUG)
      polyhedronOffsetCopyDuration += (std::chrono::steady_clock::now() - polyOffsetCopy0);
#endif
#if !defined(NDEBUG)
      polyhedronBufferPrepDuration += (std::chrono::steady_clock::now() - polyBuffer0);
#endif

#if !defined(NDEBUG)
      const auto polyUnique0 = std::chrono::steady_clock::now();
#endif
      const vtkIdType* uniqueNodeIds = nullptr;
      vtkIdType uniqueNodeCount = 0;
      if (cell.uniqueNodePoolCount > 0)
      {
        const std::size_t uniqueOffset = static_cast<std::size_t>(cell.uniqueNodePoolOffset);
        const std::size_t uniqueCount = static_cast<std::size_t>(cell.uniqueNodePoolCount);
        if (uniqueOffset + uniqueCount <= this->CellUniqueNodePool.size())
        {
          uniqueNodeIds = this->CellUniqueNodePool.data() + uniqueOffset;
          uniqueNodeCount = static_cast<vtkIdType>(uniqueCount);
        }
      }
      if (!uniqueNodeIds)
      {
        ++visitToken;
        if (visitToken == std::numeric_limits<int>::max())
        {
          std::fill(pointVisitStamp.begin(), pointVisitStamp.end(), -1);
          visitToken = 0;
        }
        uniquePointIds.clear();
        uniquePointIds.reserve(cellNodeCount);
        for (std::size_t j = 0; j < cellNodeCount; ++j)
        {
          const int nodeId = cellNodes[j];
          if (nodeId >= 0 && static_cast<std::size_t>(nodeId) < pointVisitStamp.size() &&
            pointVisitStamp[static_cast<std::size_t>(nodeId)] != visitToken)
          {
            pointVisitStamp[static_cast<std::size_t>(nodeId)] = visitToken;
            uniquePointIds.push_back(static_cast<vtkIdType>(nodeId));
          }
        }
        uniqueNodeIds = uniquePointIds.data();
        uniqueNodeCount = static_cast<vtkIdType>(uniquePointIds.size());
      }
#if !defined(NDEBUG)
      polyhedronUniqueSelectDuration += (std::chrono::steady_clock::now() - polyUnique0);
#endif

      grid[location]->InsertNextCell(
        VTK_POLYHEDRON, uniqueNodeCount, uniqueNodeIds, faces);
#if !defined(NDEBUG)
      polyhedronWriteDuration += (std::chrono::steady_clock::now() - polyhedronStep0);
      ++polyhedronCellCount;
#endif
    }
  }

#if !defined(NDEBUG)
  if (polyhedronCellCount > 0)
  {
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(polyhedronWriteDuration)
                      .count();
    FluentCffDebugLog(
      "polyhedron vtkIdTypeArray WritePointer + VTK_POLYHEDRON InsertNextCell total cells=" +
      std::to_string(polyhedronCellCount) + " " + std::to_string(ms) + " ms");
    const auto prepMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(polyhedronBufferPrepDuration).count();
    const auto nodeCopyMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(polyhedronNodeCopyDuration).count();
    const auto offsetCopyMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(polyhedronOffsetCopyDuration).count();
    const auto uniqueSelectMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(polyhedronUniqueSelectDuration).count();
    const auto setDataMs =
      std::chrono::duration_cast<std::chrono::milliseconds>(polyhedronSetDataDuration).count();
    FluentCffDebugLog("polyhedron buffer prepare " + std::to_string(prepMs) +
      " ms; faces->SetData " + std::to_string(setDataMs) + " ms");
    FluentCffDebugLog("polyhedron node copy " + std::to_string(nodeCopyMs) +
      " ms; offset copy " + std::to_string(offsetCopyMs) + " ms; unique select " +
      std::to_string(uniqueSelectMs) + " ms");
  }
#endif

  for (const auto& dataChunk : this->CellDataChunks)
  {
    if (!this->CellDataArraySelection->ArrayIsEnabled(dataChunk.variableName.c_str()))
    {
      continue;
    }
    if (dataChunk.variableName.find("UDM") != std::string::npos)
    {
      this->ParseUDMData(grid, dataChunk);
    }
    else
    {
#if !defined(NDEBUG)
      const auto cellDataChunk0 = std::chrono::steady_clock::now();
#endif
      for (std::size_t location = 0; location < this->CellZones.size(); location++)
      {
        vtkNew<vtkDoubleArray> doubleArray;
        doubleArray->SetNumberOfComponents(static_cast<int>(dataChunk.dim));
        const std::vector<vtkIdType>& zoneCellIndices = this->CellIndicesByZone[location];
        const vtkIdType tupleCount = static_cast<vtkIdType>(zoneCellIndices.size());
        double* tuplePtr =
          doubleArray->WritePointer(0, tupleCount * static_cast<vtkIdType>(dataChunk.dim));
        vtkIdType tupleOffset = 0;
        for (vtkIdType cellId : zoneCellIndices)
        {
          const std::size_t idxCell = static_cast<std::size_t>(cellId);
          for (std::size_t dim = 0; dim < dataChunk.dim; dim++)
          {
            tuplePtr[tupleOffset++] = dataChunk.dataVector[dim + dataChunk.dim * idxCell];
          }
        }
        doubleArray->SetName(dataChunk.variableName.c_str());
        grid[location]->GetCellData()->AddArray(doubleArray);
      }
#if !defined(NDEBUG)
      {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
          std::chrono::steady_clock::now() - cellDataChunk0)
                          .count();
        FluentCffDebugLog("cell data vtkDoubleArray WritePointer chunk=\"" + dataChunk.variableName +
          "\" " + std::to_string(ms) + " ms");
      }
#endif
    }
  }

  for (std::size_t location = 0; location < this->CellZones.size(); location++)
  {
    grid[location]->SetPoints(this->Points);
    output->SetBlock(static_cast<unsigned int>(location), grid[location]);
    grid[location]->Delete();
  }

  return 1;
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::ParseUDMData(
  std::vector<vtkSmartPointer<vtkUnstructuredGrid>>& grid, const DataChunk& dataChunk)
{
  if (this->CellIndicesByZone.size() != this->CellZones.size())
  {
    this->CellIndicesByZone.clear();
    this->CellIndicesByZone.resize(this->CellZones.size());
    for (vtkIdType cellId = 0; cellId < static_cast<vtkIdType>(this->Cells.size()); ++cellId)
    {
      for (std::size_t location = 0; location < this->CellZones.size(); ++location)
      {
        if (this->Cells[static_cast<std::size_t>(cellId)].zone == this->CellZones[location])
        {
          this->CellIndicesByZone[location].push_back(cellId);
          break;
        }
      }
    }
  }
#if !defined(NDEBUG)
  const auto udm0 = std::chrono::steady_clock::now();
#endif
  for (std::size_t location = 0; location < this->CellZones.size(); location++)
  {
    for (std::size_t dim = 0; dim < dataChunk.dim; dim++)
    {
      vtkNew<vtkDoubleArray> doubleArray;
      doubleArray->SetNumberOfComponents(1);
      const std::vector<vtkIdType>& zoneCellIndices = this->CellIndicesByZone[location];
      const vtkIdType valueCount = static_cast<vtkIdType>(zoneCellIndices.size());
      double* valuePtr = doubleArray->WritePointer(0, valueCount);
      vtkIdType valueOffset = 0;

      for (vtkIdType cellId : zoneCellIndices)
      {
        const std::size_t idxCell = static_cast<std::size_t>(cellId);
        valuePtr[valueOffset++] = dataChunk.dataVector[dim + dataChunk.dim * idxCell];
      }
      doubleArray->SetName((dataChunk.variableName + "_" + std::to_string(dim)).c_str());
      grid[location]->GetCellData()->AddArray(doubleArray);
    }
  }
#if !defined(NDEBUG)
  {
    const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - udm0)
        .count();
    FluentCffDebugLog(
      "ParseUDMData chunk=\"" + dataChunk.variableName + "\" " + std::to_string(ms) + " ms");
  }
#endif
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::PrintSelf(ostream& os, vtkIndent indent)
{
  this->Superclass::PrintSelf(os, indent);
  os << indent << "File Name: " << this->FileName << endl;
  os << indent << "Data File Name: "
     << (this->DataFileName.empty() ? std::string("<auto>") : this->DataFileName) << endl;
  os << indent << "Number Of Cells: " << this->NumberOfCells << endl;
  os << indent << "Number Of cell Zone: " << this->CellZones.size() << endl;
  if (this->FileState != DataState::NOT_LOADED)
  {
    os << indent << "Cell Array Count: " << this->CellDataChunks.size() << endl;
    if (!this->CellDataChunks.empty())
    {
      os << indent;
      for (const auto& dataChunk : this->CellDataChunks)
      {
        os << dataChunk.variableName;
      }
      os << endl;
    }
    os << indent << "Face Array Count: " << this->FaceDataChunks.size() << endl;
  }
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::RequestInformation(vtkInformation* vtkNotUsed(request),
  vtkInformationVector** vtkNotUsed(inputVector), vtkInformationVector* vtkNotUsed(outputVector))
{
  if (this->FileName.empty())
  {
    vtkErrorMacro("FileName has to be specified!");
    return 0;
  }

  if (!this->OpenCaseFile(this->FileName))
  {
    vtkErrorMacro("Unable to open case file.");
    return 0;
  }

  this->ClearData();
  this->FileState = this->OpenDataFile(this->FileName);
  if (this->FileState == DataState::NOT_LOADED)
  {
    vtkWarningMacro("No data file (.dat.h5) found. Only the case file will be opened.");
  }
  if (this->FileState == DataState::ERROR)
  {
    vtkErrorMacro("The data file associated to " << this->FileName << " is not a HDF5 file.");
    return 0;
  }

  this->GridDimension = this->GetDimension();
  if (this->GridDimension == 0)
    return 0;
  vtkDebugMacro(<< "\nDimension of file " << this->GridDimension);

  if (this->FileState == DataState::AVAILABLE)
  {
    int flagData = 0;
    try
    {
      flagData = this->GetMetaData();
    }
    catch (std::runtime_error const& e)
    {
      vtkErrorMacro(<< e.what());
      return 0;
    }
    if (flagData == 0)
    {
      vtkErrorMacro(
        "Unable to read the Data CFF file. The structure of the file may have changed.");
      return 0;
    }
    for (const auto& variableName : this->PreReadCellData)
    {
      this->CellDataArraySelection->AddArray(variableName.c_str());
    }
    for (const auto& variableName : this->PreReadFaceData)
    {
      this->FaceDataArraySelection->AddArray(variableName.c_str());
    }
  }

  return 1;
}

//------------------------------------------------------------------------------
bool vtkFLUENTCFFReader::OpenCaseFile(const std::string& filename)
{
  if (this->HDFImpl->FluentCaseFile >= 0)
  {
    H5Fclose(this->HDFImpl->FluentCaseFile);
    this->HDFImpl->FluentCaseFile = -1;
  }
  // Check if hdf5 lib contains zlib (DEFLATE)
  htri_t avail = H5Zfilter_avail(H5Z_FILTER_DEFLATE);
  if (!avail)
  {
    vtkErrorMacro("The current build is not compatible with this reader, HDF5 library misses ZLIB "
                  "compatibility.");
    return false;
  }
  // Check if the file is HDF5 or exist
  htri_t file_type = H5Fis_hdf5(filename.c_str());
  if (file_type != 1)
  {
    vtkErrorMacro("The file " << filename << " does not exist or is not a HDF5 file.");
    return false;
  }
  // Open file with default properties access
  this->HDFImpl->FluentCaseFile = H5Fopen(filename.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
  // Check if file is CFF Format like
  herr_t s1 = H5Gget_objinfo(this->HDFImpl->FluentCaseFile, "/meshes", false, nullptr);
  herr_t s2 = H5Gget_objinfo(this->HDFImpl->FluentCaseFile, "/settings", false, nullptr);
  if (s1 == 0 && s2 == 0)
  {
    return true;
  }
  else
  {
    vtkErrorMacro("The file " << filename << " is not a CFF Fluent file.");
    return false;
  }
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetNumberOfCellArrays()
{
  return this->CellDataArraySelection->GetNumberOfArrays();
}

//------------------------------------------------------------------------------
const char* vtkFLUENTCFFReader::GetCellArrayName(int index)
{
  return this->CellDataArraySelection->GetArrayName(index);
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetCellArrayStatus(const char* name)
{
  return this->CellDataArraySelection->ArrayIsEnabled(name);
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::SetCellArrayStatus(const char* name, int stat)
{
  if (stat)
  {
    this->CellDataArraySelection->EnableArray(name);
  }
  else
  {
    this->CellDataArraySelection->DisableArray(name);
  }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::EnableAllCellArrays()
{
  this->CellDataArraySelection->EnableAllArrays();
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::DisableAllCellArrays()
{
  this->CellDataArraySelection->DisableAllArrays();
}

//------------------------------------------------------------------------------
vtkFLUENTCFFReader::DataState vtkFLUENTCFFReader::OpenDataFile(const std::string& filename)
{
  if (this->HDFImpl->FluentDataFile >= 0)
  {
    H5Fclose(this->HDFImpl->FluentDataFile);
    this->HDFImpl->FluentDataFile = -1;
  }

  std::string dfilename = this->DataFileName;
  if (dfilename.empty())
  {
    // dfilename represent the dat file name (extension .dat.h5)
    // when opening a .cas.h5, it will automatically open the associated .dat.h5 (if exist)
    // filename.cas.h5 -> filename.dat.h5
    dfilename = filename;
    dfilename.erase(dfilename.length() - 6, 6);
    dfilename.append("dat.h5");
  }

  // Check if the file is HDF5 or exist
  htri_t file_type = H5Fis_hdf5(dfilename.c_str());
  // If there is a file but is not HDF5
  if (file_type == 0)
  {
    return DataState::ERROR;
  }
  // If there is no file, read only the case file
  if (file_type < 0)
  {
    return DataState::NOT_LOADED;
  }

  // Open file with default properties access
  this->HDFImpl->FluentDataFile = H5Fopen(dfilename.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
  return DataState::AVAILABLE;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetNumberOfFaceArrays()
{
  return this->FaceDataArraySelection->GetNumberOfArrays();
}

//------------------------------------------------------------------------------
const char* vtkFLUENTCFFReader::GetFaceArrayName(int index)
{
  return this->FaceDataArraySelection->GetArrayName(index);
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetFaceArrayStatus(const char* name)
{
  return this->FaceDataArraySelection->ArrayIsEnabled(name);
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::SetFaceArrayStatus(const char* name, int stat)
{
  if (stat)
  {
    this->FaceDataArraySelection->EnableArray(name);
  }
  else
  {
    this->FaceDataArraySelection->DisableArray(name);
  }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::EnableAllFaceArrays()
{
  this->FaceDataArraySelection->EnableAllArrays();
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::DisableAllFaceArrays()
{
  this->FaceDataArraySelection->DisableAllArrays();
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetNumberOfFaceZones() const
{
  return static_cast<int>(this->FaceZones.size());
}

//------------------------------------------------------------------------------
const char* vtkFLUENTCFFReader::GetFaceZoneName(int index) const
{
  return (index >= 0 && static_cast<std::size_t>(index) < this->FaceZones.size())
    ? this->FaceZones[static_cast<std::size_t>(index)].name.c_str()
    : nullptr;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetFaceZoneIndexByName(const char* name) const
{
  if (!name)
  {
    return -1;
  }
  for (std::size_t i = 0; i < this->FaceZones.size(); ++i)
  {
    if (this->FaceZones[i].name == name)
    {
      return static_cast<int>(i);
    }
  }
  return -1;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetFaceZoneIdByName(const char* name) const
{
  const int index = this->GetFaceZoneIndexByName(name);
  return index >= 0 ? this->FaceZones[static_cast<std::size_t>(index)].id : -1;
}

//------------------------------------------------------------------------------
vtkIdType vtkFLUENTCFFReader::GetFaceZoneFirstFaceIdByName(const char* name) const
{
  const int index = this->GetFaceZoneIndexByName(name);
  return index >= 0 ? this->FaceZones[static_cast<std::size_t>(index)].firstFaceId : -1;
}

//------------------------------------------------------------------------------
vtkIdType vtkFLUENTCFFReader::GetFaceZoneLastFaceIdByName(const char* name) const
{
  const int index = this->GetFaceZoneIndexByName(name);
  return index >= 0 ? this->FaceZones[static_cast<std::size_t>(index)].lastFaceId : -1;
}

//------------------------------------------------------------------------------
vtkIdType vtkFLUENTCFFReader::GetFaceZoneSizeByName(const char* name) const
{
  const int index = this->GetFaceZoneIndexByName(name);
  if (index < 0)
  {
    return 0;
  }
  const auto& info = this->FaceZones[static_cast<std::size_t>(index)];
  return info.firstFaceId >= 0 && info.lastFaceId >= info.firstFaceId
    ? info.lastFaceId - info.firstFaceId + 1
    : 0;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetFaceIdByZoneName(const char* name, vtkIdType localFaceIndex) const
{
  const int index = this->GetFaceZoneIndexByName(name);
  if (index < 0 || localFaceIndex < 0)
  {
    return -1;
  }
  const auto& info = this->FaceZones[static_cast<std::size_t>(index)];
  const vtkIdType faceId = info.firstFaceId + localFaceIndex;
  return (faceId >= info.firstFaceId && faceId <= info.lastFaceId) ? static_cast<int>(faceId) : -1;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetCellZoneCount() const
{
  return static_cast<int>(this->CellZones.size());
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetCellZoneIdAtIndex(int index) const
{
  if (index < 0 || static_cast<std::size_t>(index) >= this->CellZones.size())
  {
    return -1;
  }
  return this->CellZones[static_cast<std::size_t>(index)];
}

//------------------------------------------------------------------------------
vtkIdType vtkFLUENTCFFReader::GetNumberOfNodesRead() const
{
  return this->Points->GetNumberOfPoints();
}

//------------------------------------------------------------------------------
vtkIdType vtkFLUENTCFFReader::GetNumberOfFacesRead() const
{
  return static_cast<vtkIdType>(this->Faces.size());
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetCellType(vtkIdType cellId) const
{
  return (cellId >= 0 && static_cast<std::size_t>(cellId) < this->Cells.size())
    ? this->Cells[static_cast<std::size_t>(cellId)].type
    : -1;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetCellZoneId(vtkIdType cellId) const
{
  return (cellId >= 0 && static_cast<std::size_t>(cellId) < this->Cells.size())
    ? this->Cells[static_cast<std::size_t>(cellId)].zone
    : -1;
}

//------------------------------------------------------------------------------
vtkIdType vtkFLUENTCFFReader::GetCellNumberOfFaces(vtkIdType cellId) const
{
  return (cellId >= 0 && static_cast<std::size_t>(cellId) < this->Cells.size())
    ? static_cast<vtkIdType>(this->Cells[static_cast<std::size_t>(cellId)].faces.size())
    : 0;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetCellFaceId(vtkIdType cellId, vtkIdType localFaceId) const
{
  if (cellId < 0 || static_cast<std::size_t>(cellId) >= this->Cells.size())
  {
    return -1;
  }
  const auto& faces = this->Cells[static_cast<std::size_t>(cellId)].faces;
  return (localFaceId >= 0 && static_cast<std::size_t>(localFaceId) < faces.size())
    ? faces[static_cast<std::size_t>(localFaceId)]
    : -1;
}

//------------------------------------------------------------------------------
vtkIdType vtkFLUENTCFFReader::GetCellNumberOfNodes(vtkIdType cellId) const
{
  if (cellId < 0 || static_cast<std::size_t>(cellId) >= this->Cells.size())
  {
    return 0;
  }
  const auto& cell = this->Cells[static_cast<std::size_t>(cellId)];
  return cell.nodePoolCount > 0 ? static_cast<vtkIdType>(cell.nodePoolCount)
                                : static_cast<vtkIdType>(cell.nodes.size());
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetCellNodeId(vtkIdType cellId, vtkIdType localNodeId) const
{
  if (cellId < 0 || static_cast<std::size_t>(cellId) >= this->Cells.size())
  {
    return -1;
  }
  const auto& cell = this->Cells[static_cast<std::size_t>(cellId)];
  if (cell.nodePoolCount > 0)
  {
    if (localNodeId < 0 || localNodeId >= static_cast<vtkIdType>(cell.nodePoolCount))
    {
      return -1;
    }
    const std::size_t offset = static_cast<std::size_t>(cell.nodePoolOffset + localNodeId);
    return offset < this->CellNodePool.size() ? this->CellNodePool[offset] : -1;
  }
  const auto& nodes = cell.nodes;
  return (localNodeId >= 0 && static_cast<std::size_t>(localNodeId) < nodes.size())
    ? nodes[static_cast<std::size_t>(localNodeId)]
    : -1;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetFaceType(vtkIdType faceId) const
{
  return (faceId >= 0 && static_cast<std::size_t>(faceId) < this->Faces.size())
    ? this->Faces[static_cast<std::size_t>(faceId)].type
    : -1;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetFaceZoneId(vtkIdType faceId) const
{
  return (faceId >= 0 && static_cast<std::size_t>(faceId) < this->Faces.size())
    ? static_cast<int>(this->Faces[static_cast<std::size_t>(faceId)].zone)
    : -1;
}

//------------------------------------------------------------------------------
vtkIdType vtkFLUENTCFFReader::GetFaceNumberOfNodes(vtkIdType faceId) const
{
  return (faceId >= 0 && static_cast<std::size_t>(faceId) < this->Faces.size())
    ? static_cast<vtkIdType>(this->Faces[static_cast<std::size_t>(faceId)].nodeCount)
    : 0;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetFaceNodeId(vtkIdType faceId, vtkIdType localNodeId) const
{
  if (faceId < 0 || static_cast<std::size_t>(faceId) >= this->Faces.size())
  {
    return -1;
  }
  const auto& face = this->Faces[static_cast<std::size_t>(faceId)];
  if (localNodeId < 0 || localNodeId >= static_cast<vtkIdType>(face.nodeCount))
  {
    return -1;
  }
  const std::size_t offset = static_cast<std::size_t>(face.nodeOffset + localNodeId);
  return offset < this->FaceNodePool.size() ? this->FaceNodePool[offset] : -1;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetFaceC0(vtkIdType faceId) const
{
  return (faceId >= 0 && static_cast<std::size_t>(faceId) < this->Faces.size())
    ? this->Faces[static_cast<std::size_t>(faceId)].c0
    : -1;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetFaceC1(vtkIdType faceId) const
{
  return (faceId >= 0 && static_cast<std::size_t>(faceId) < this->Faces.size())
    ? this->Faces[static_cast<std::size_t>(faceId)].c1
    : -1;
}

//------------------------------------------------------------------------------
bool vtkFLUENTCFFReader::GetNodeCoordinates(vtkIdType nodeId, double coords[3]) const
{
  if (nodeId < 0 || nodeId >= this->Points->GetNumberOfPoints())
  {
    return false;
  }
  this->Points->GetPoint(nodeId, coords);
  return true;
}

//------------------------------------------------------------------------------
const vtkFLUENTCFFReader::DataChunk* vtkFLUENTCFFReader::FindDataChunk(
  const std::vector<DataChunk>& chunks, const char* name) const
{
  if (!name)
  {
    return nullptr;
  }
  for (const auto& chunk : chunks)
  {
    if (chunk.variableName == name)
    {
      return &chunk;
    }
  }
  return nullptr;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetCellArrayComponents(const char* name) const
{
  const DataChunk* chunk = this->FindDataChunk(this->CellDataChunks, name);
  return chunk ? static_cast<int>(chunk->dim) : 0;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetFaceArrayComponents(const char* name) const
{
  const DataChunk* chunk = this->FindDataChunk(this->FaceDataChunks, name);
  return chunk ? static_cast<int>(chunk->dim) : 0;
}

//------------------------------------------------------------------------------
double vtkFLUENTCFFReader::GetCellArrayValue(const char* name, vtkIdType cellId, int component) const
{
  const DataChunk* chunk = this->FindDataChunk(this->CellDataChunks, name);
  if (!chunk || cellId < 0 || component < 0 || static_cast<std::size_t>(component) >= chunk->dim)
  {
    return std::numeric_limits<double>::quiet_NaN();
  }
  const std::size_t offset = static_cast<std::size_t>(cellId) * chunk->dim +
    static_cast<std::size_t>(component);
  return offset < chunk->dataVector.size() ? chunk->dataVector[offset]
                                           : std::numeric_limits<double>::quiet_NaN();
}

//------------------------------------------------------------------------------
double vtkFLUENTCFFReader::GetFaceArrayValue(const char* name, vtkIdType faceId, int component) const
{
  const DataChunk* chunk = this->FindDataChunk(this->FaceDataChunks, name);
  if (!chunk || faceId < 0 || component < 0 || static_cast<std::size_t>(component) >= chunk->dim)
  {
    return std::numeric_limits<double>::quiet_NaN();
  }
  const std::size_t offset = static_cast<std::size_t>(faceId) * chunk->dim +
    static_cast<std::size_t>(component);
  return offset < chunk->dataVector.size() ? chunk->dataVector[offset]
                                           : std::numeric_limits<double>::quiet_NaN();
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetFaceZoneIndicesOverlappingFaceArray(
  const char* faceArrayName, vtkIntArray* outZoneIndices) const
{
  if (!faceArrayName || !outZoneIndices)
  {
    return -1;
  }
  outZoneIndices->Initialize();
  outZoneIndices->SetNumberOfComponents(1);
  const DataChunk* chunk = this->FindDataChunk(this->FaceDataChunks, faceArrayName);
  if (!chunk)
  {
    return -1;
  }
  if (chunk->FaceSectionFluentIdRanges1Based.empty())
  {
    vtkWarningMacro(<< "Face array \"" << faceArrayName << "\" has no stored section id ranges; call Update() after upgrading reader.");
    return 0;
  }
  std::vector<int> hits;
  for (std::size_t zi = 0; zi < this->FaceZones.size(); ++zi)
  {
    const auto& fz = this->FaceZones[zi];
    if (fz.firstFaceId < 0 || fz.lastFaceId < fz.firstFaceId)
    {
      continue;
    }
    const std::uint64_t zMin = static_cast<std::uint64_t>(fz.firstFaceId + 1);
    const std::uint64_t zMax = static_cast<std::uint64_t>(fz.lastFaceId + 1);
    bool overlap = false;
    for (const auto& sec : chunk->FaceSectionFluentIdRanges1Based)
    {
      if (std::max(sec.first, zMin) <= std::min(sec.second, zMax))
      {
        overlap = true;
        break;
      }
    }
    if (overlap)
    {
      hits.push_back(static_cast<int>(zi));
    }
  }
  std::sort(hits.begin(), hits.end());
  for (int v : hits)
  {
    outZoneIndices->InsertNextValue(v);
  }
  return static_cast<int>(hits.size());
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::EnsureFaceZoneTopologyCache(int zoneIndex) const
{
  if (zoneIndex < 0 || static_cast<std::size_t>(zoneIndex) >= this->FaceZones.size())
  {
    return;
  }

  if (this->FaceZoneTopologyCaches.size() != this->FaceZones.size())
  {
    this->FaceZoneTopologyCaches.clear();
    this->FaceZoneTopologyCaches.resize(this->FaceZones.size());
  }

  FaceZoneTopologyCache& cache = this->FaceZoneTopologyCaches[static_cast<std::size_t>(zoneIndex)];
  if (cache.Built && cache.Polys)
  {
    return;
  }

  cache.FaceIds.clear();
  vtkNew<vtkCellArray> polys;
  const auto& info = this->FaceZones[static_cast<std::size_t>(zoneIndex)];
  for (vtkIdType faceId = info.firstFaceId; faceId <= info.lastFaceId; ++faceId)
  {
    if (faceId < 0 || static_cast<std::size_t>(faceId) >= this->Faces.size())
    {
      continue;
    }
    const auto& face = this->Faces[static_cast<std::size_t>(faceId)];
    if (face.nodeCount < 3)
    {
      continue;
    }
    const std::size_t offset = static_cast<std::size_t>(face.nodeOffset);
    if (offset + static_cast<std::size_t>(face.nodeCount) > this->FaceNodePool.size())
    {
      continue;
    }
    const int* nodes = this->FaceNodePool.data() + offset;

    vtkNew<vtkPolygon> polygon;
    polygon->GetPointIds()->SetNumberOfIds(static_cast<vtkIdType>(face.nodeCount));
    for (int i = 0; i < face.nodeCount; ++i)
    {
      polygon->GetPointIds()->SetId(static_cast<vtkIdType>(i), nodes[i]);
    }
    polys->InsertNextCell(polygon);
    cache.FaceIds.push_back(faceId);
  }

  cache.Polys = polys;
  cache.Built = true;
}

//------------------------------------------------------------------------------
vtkSmartPointer<vtkPolyData> vtkFLUENTCFFReader::CreateFaceZonePolyData(
  const char* zoneName, const char* faceArrayName, int component) const
{
  vtkSmartPointer<vtkPolyData> polyData = vtkSmartPointer<vtkPolyData>::New();
  const int zoneIndex = this->GetFaceZoneIndexByName(zoneName);
  if (zoneIndex < 0)
  {
    return polyData;
  }

  this->EnsureFaceZoneTopologyCache(zoneIndex);
  const FaceZoneTopologyCache& cache =
    this->FaceZoneTopologyCaches[static_cast<std::size_t>(zoneIndex)];

  vtkNew<vtkDoubleArray> scalars;
  const DataChunk* faceChunk = nullptr;
  if (faceArrayName)
  {
    scalars->SetName(faceArrayName);
    scalars->SetNumberOfComponents(1);
    scalars->WritePointer(0, static_cast<vtkIdType>(cache.FaceIds.size()));
    faceChunk = this->FindDataChunk(this->FaceDataChunks, faceArrayName);
  }
  double* scalarPtr = faceArrayName ? scalars->GetPointer(0) : nullptr;
  vtkIdType scalarOffset = 0;
  const bool componentInRange =
    component >= 0 && faceChunk && static_cast<std::size_t>(component) < faceChunk->dim;
  const double nanValue = std::numeric_limits<double>::quiet_NaN();

#if !defined(NDEBUG)
  const auto faceZonePoly0 = std::chrono::steady_clock::now();
#endif
  for (vtkIdType faceId : cache.FaceIds)
  {
    if (faceArrayName)
    {
      double value = nanValue;
      if (componentInRange && faceId >= 0)
      {
        const std::size_t offset = static_cast<std::size_t>(faceId) * faceChunk->dim +
          static_cast<std::size_t>(component);
        if (offset < faceChunk->dataVector.size())
        {
          value = faceChunk->dataVector[offset];
        }
      }
      scalarPtr[scalarOffset++] = value;
    }
  }
#if !defined(NDEBUG)
  FluentCffDebugPrintMs(
    "CreateFaceZonePolyData polys + vtkDoubleArray face scalars (GetFaceArrayValue)", faceZonePoly0);
#endif

  polyData->SetPoints(this->Points);
  polyData->SetPolys(cache.Polys);
  if (faceArrayName)
  {
    polyData->GetCellData()->SetScalars(scalars);
  }
  return polyData;
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::GetNumberOfCellZones()
{
  for (const auto& cell : this->Cells)
  {
    if (this->CellZones.empty())
    {
      this->CellZones.push_back(cell.zone);
    }
    else
    {
      int match = 0;
      for (const auto& CellZone : CellZones)
      {
        if (CellZone == cell.zone)
        {
          match = 1;
        }
      }
      if (match == 0)
      {
        this->CellZones.push_back(cell.zone);
      }
    }
  }
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::ParseCaseFile()
{
  try
  {
    this->GetNodesGlobal();
    this->GetCellsGlobal();
    this->GetFacesGlobal();
    // .cas is always DP
    // .dat is DP or SP
    this->GetNodes();
    this->GetCells();
    this->GetFaces();

    this->GetCellTree();
    this->GetCellOverset();
    this->GetFaceTree();
    this->GetInterfaceFaceParents();
    this->GetNonconformalGridInterfaceFaceInformation();
  }
  catch (std::runtime_error const& e)
  {
    vtkErrorMacro(<< e.what());
    return 0;
  }
  return 1;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetDimension()
{
  hid_t group, attr;
  int32_t dimension;
  group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1", H5P_DEFAULT);
  if (group < 0)
  {
    vtkErrorMacro("Unable to open HDF group (GetDimension).");
    return 0;
  }
  attr = H5Aopen(group, "dimension", H5P_DEFAULT);
  if (attr < 0)
  {
    vtkErrorMacro("Unable to open HDF attribute (GetDimension).");
    return 0;
  }
  if (H5Aread(attr, H5T_NATIVE_INT32, &dimension) < 0)
  {
    vtkErrorMacro("Unable to read HDF attribute (GetDimension).");
    return 0;
  }
  if (H5Aclose(attr))
  {
    vtkErrorMacro("Unable to close HDF attribute (GetDimension).");
    return 0;
  }
  if (H5Gclose(group))
  {
    vtkErrorMacro("Unable to close HDF group (GetDimension).");
    return 0;
  }
  return static_cast<int>(dimension);
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::GetNodesGlobal()
{
  hid_t group, attr;
  uint64_t firstIndex, lastIndex;
  group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1", H5P_DEFAULT);
  if (group < 0)
  {
    throw std::runtime_error("Unable to open HDF group (GetNodesGlobal).");
  }
  attr = H5Aopen(group, "nodeOffset", H5P_DEFAULT);
  if (attr < 0)
  {
    throw std::runtime_error("Unable to open HDF attribute (GetNodesGlobal).");
  }
  CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &firstIndex));
  CHECK_HDF(H5Aclose(attr));
  attr = H5Aopen(group, "nodeCount", H5P_DEFAULT);
  if (attr < 0)
  {
    throw std::runtime_error("Unable to open HDF attribute (GetNodesGlobal).");
  }
  CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &lastIndex));
  CHECK_HDF(H5Aclose(attr));
  CHECK_HDF(H5Gclose(group));
#if !defined(NDEBUG)
  const auto gng0 = std::chrono::steady_clock::now();
#endif
  vtkNew<vtkDoubleArray> coordinates;
  coordinates->SetNumberOfComponents(3);
  coordinates->WritePointer(0, static_cast<vtkIdType>(lastIndex) * 3);
  this->Points->SetData(coordinates);
#if !defined(NDEBUG)
  FluentCffDebugPrintMs("GetNodesGlobal vtkDoubleArray WritePointer + Points::SetData", gng0);
#endif
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::GetNodes()
{
  hid_t group, attr, dset;
  uint64_t nZones;
  group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1/nodes/zoneTopology", H5P_DEFAULT);
  if (group < 0)
  {
    throw std::runtime_error("Unable to open HDF group (GetNodes).");
  }
  attr = H5Aopen(group, "nZones", H5P_DEFAULT);
  if (attr < 0)
  {
    throw std::runtime_error("Unable to open HDF attribute (GetNodes).");
  }
  CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &nZones));
  CHECK_HDF(H5Aclose(attr));

  std::vector<uint64_t> minId(nZones);
  dset = H5Dopen(group, "minId", H5P_DEFAULT);
  if (dset < 0)
  {
    throw std::runtime_error("Unable to open HDF dataset (GetNodes).");
  }
  CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, minId.data()));
  CHECK_HDF(H5Dclose(dset));

  std::vector<uint64_t> maxId(nZones);
  dset = H5Dopen(group, "maxId", H5P_DEFAULT);
  if (dset < 0)
  {
    throw std::runtime_error("Unable to open HDF dataset (GetNodes).");
  }
  CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, maxId.data()));
  CHECK_HDF(H5Dclose(dset));

  std::vector<int32_t> Id(nZones);
  dset = H5Dopen(group, "id", H5P_DEFAULT);
  if (dset < 0)
  {
    throw std::runtime_error("Unable to open HDF dataset (GetNodes).");
  }
  CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, Id.data()));
  CHECK_HDF(H5Dclose(dset));

  std::vector<std::string> zoneNames;
  dset = H5Dopen(group, "name", H5P_DEFAULT);
  if (dset >= 0)
  {
    hid_t spaceName = H5Dget_space(dset);
    hid_t dataTypeName = H5Dget_type(dset);
    std::size_t stringLength = H5Tget_size(dataTypeName);
    char* strchar = new char[stringLength];
    CHECK_HDF(H5Dread(dset, dataTypeName, H5S_ALL, H5S_ALL, H5P_DEFAULT, strchar));
    zoneNames = SplitFieldNames(std::string(strchar));
    delete[] strchar;
    CHECK_HDF(H5Tclose(dataTypeName));
    CHECK_HDF(H5Sclose(spaceName));
    CHECK_HDF(H5Dclose(dset));
  }

  std::vector<uint64_t> dimension(nZones);
  dset = H5Dopen(group, "dimension", H5P_DEFAULT);
  if (dset < 0)
  {
    throw std::runtime_error("Unable to open HDF dataset (GetNodes).");
  }
  CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, dimension.data()));
  CHECK_HDF(H5Dclose(dset));

#if !defined(NDEBUG)
  std::chrono::steady_clock::duration getNodesPointWriteDuration{};
#endif

  for (uint64_t iZone = 0; iZone < nZones; iZone++)
  {
    uint64_t coords_minId, coords_maxId;
    hid_t group_coords, dset_coords;
    group_coords = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1/nodes/coords", H5P_DEFAULT);
    if (group_coords < 0)
    {
      throw std::runtime_error("Unable to open HDF group (GetNodes coords).");
    }
    dset_coords = H5Dopen(group_coords, std::to_string(Id[iZone]).c_str(), H5P_DEFAULT);
    if (dset_coords < 0)
    {
      throw std::runtime_error("Unable to open HDF group (GetNodes coords).");
    }

    attr = H5Aopen(dset_coords, "minId", H5P_DEFAULT);
    if (attr < 0)
    {
      throw std::runtime_error("Unable to open HDF attribute (GetNodes coords).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &coords_minId));
    CHECK_HDF(H5Aclose(attr));
    attr = H5Aopen(dset_coords, "maxId", H5P_DEFAULT);
    if (attr < 0)
    {
      throw std::runtime_error("Unable to open HDF attribute (GetNodes coords).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &coords_maxId));
    CHECK_HDF(H5Aclose(attr));
    unsigned int firstIndex = static_cast<unsigned int>(coords_minId);
    unsigned int lastIndex = static_cast<unsigned int>(coords_maxId);

    uint64_t size = lastIndex - firstIndex + 1;
    uint64_t gSize;
    if (this->GridDimension == 3)
    {
      gSize = size * 3;
    }
    else
    {
      gSize = size * 2;
    }

    std::vector<double> nodeData(gSize);
    CHECK_HDF(
      H5Dread(dset_coords, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, nodeData.data()));
    CHECK_HDF(H5Dclose(dset_coords));
    CHECK_HDF(H5Gclose(group_coords));

    auto* pointData = vtkDoubleArray::SafeDownCast(this->Points->GetData());
    double* pointPtr = pointData ? pointData->GetPointer(0) : nullptr;
    if (pointPtr == nullptr)
    {
      throw std::runtime_error("Unable to access vtkPoints storage (GetNodes).");
    }

#if !defined(NDEBUG)
    const auto pointWriteStep0 = std::chrono::steady_clock::now();
#endif
    if (this->GridDimension == 3)
    {
      for (unsigned int i = firstIndex; i <= lastIndex; i++)
      {
        vtkIdType pointIndex = static_cast<vtkIdType>(i - 1) * 3;
        pointPtr[pointIndex + 0] = nodeData[(i - firstIndex) * 3 + 0];
        pointPtr[pointIndex + 1] = nodeData[(i - firstIndex) * 3 + 1];
        pointPtr[pointIndex + 2] = nodeData[(i - firstIndex) * 3 + 2];
      }
    }
    else
    {
      for (unsigned int i = firstIndex; i <= lastIndex; i++)
      {
        vtkIdType pointIndex = static_cast<vtkIdType>(i - 1) * 3;
        pointPtr[pointIndex + 0] = nodeData[(i - firstIndex) * 2 + 0];
        pointPtr[pointIndex + 1] = nodeData[(i - firstIndex) * 2 + 1];
        pointPtr[pointIndex + 2] = 0.0;
      }
    }
#if !defined(NDEBUG)
    getNodesPointWriteDuration += (std::chrono::steady_clock::now() - pointWriteStep0);
#endif
  }

#if !defined(NDEBUG)
  {
    const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(getNodesPointWriteDuration).count();
    FluentCffDebugLog(
      "GetNodes vtkPoints underlying double buffer fill " + std::to_string(ms) + " ms");
  }
#endif

  CHECK_HDF(H5Gclose(group));
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::GetCellsGlobal()
{
  hid_t group, attr;
  uint64_t firstIndex, lastIndex;
  group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1", H5P_DEFAULT);
  if (group < 0)
  {
    throw std::runtime_error("Unable to open HDF group (GetCellsGlobal).");
  }
  attr = H5Aopen(group, "cellOffset", H5P_DEFAULT);
  if (attr < 0)
  {
    throw std::runtime_error("Unable to open HDF attribute (GetCellsGlobal).");
  }
  CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &firstIndex));
  CHECK_HDF(H5Aclose(attr));
  attr = H5Aopen(group, "cellCount", H5P_DEFAULT);
  if (attr < 0)
  {
    throw std::runtime_error("Unable to open HDF attribute (GetCellsGlobal).");
  }
  CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &lastIndex));
  CHECK_HDF(H5Aclose(attr));
  CHECK_HDF(H5Gclose(group));
  this->Cells.resize(lastIndex);
  // Pre-size commonly used per-cell connectivity vectors to reduce reallocations.
  for (auto& cell : this->Cells)
  {
    cell.faces.reserve(6);
    cell.nodes.reserve(8);
    cell.nodesOffset.reserve(8);
    cell.childId.reserve(4);
  }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::GetCells()
{
  hid_t group, attr, dset;
  uint64_t nZones;
  group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1/cells/zoneTopology", H5P_DEFAULT);
  if (group < 0)
  {
    throw std::runtime_error("Unable to open HDF group (GetCells).");
  }
  attr = H5Aopen(group, "nZones", H5P_DEFAULT);
  if (attr < 0)
  {
    throw std::runtime_error("Unable to open HDF attribute (GetCells).");
  }
  CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &nZones));
  CHECK_HDF(H5Aclose(attr));

  std::vector<uint64_t> minId(nZones);
  dset = H5Dopen(group, "minId", H5P_DEFAULT);
  if (dset < 0)
  {
    throw std::runtime_error("Unable to open HDF dataset (GetCells).");
  }
  CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, minId.data()));
  CHECK_HDF(H5Dclose(dset));

  std::vector<uint64_t> maxId(nZones);
  dset = H5Dopen(group, "maxId", H5P_DEFAULT);
  if (dset < 0)
  {
    throw std::runtime_error("Unable to open HDF dataset (GetCells).");
  }
  CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, maxId.data()));
  CHECK_HDF(H5Dclose(dset));

  std::vector<int32_t> Id(nZones);
  dset = H5Dopen(group, "id", H5P_DEFAULT);
  if (dset < 0)
  {
    throw std::runtime_error("Unable to open HDF dataset (GetCells).");
  }
  CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, Id.data()));
  CHECK_HDF(H5Dclose(dset));

  std::vector<uint64_t> dimension(nZones);
  dset = H5Dopen(group, "dimension", H5P_DEFAULT);
  if (dset < 0)
  {
    throw std::runtime_error("Unable to open HDF dataset (GetCells).");
  }
  CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, dimension.data()));
  CHECK_HDF(H5Dclose(dset));

  std::vector<int32_t> cellType(nZones);
  dset = H5Dopen(group, "cellType", H5P_DEFAULT);
  if (dset < 0)
  {
    throw std::runtime_error("Unable to open HDF dataset (GetCells).");
  }
  CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, cellType.data()));
  CHECK_HDF(H5Dclose(dset));

  std::vector<int32_t> childZoneId(nZones);
  dset = H5Dopen(group, "childZoneId", H5P_DEFAULT);
  if (dset < 0)
  {
    throw std::runtime_error("Unable to open HDF dataset (GetCells).");
  }
  CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, childZoneId.data()));
  CHECK_HDF(H5Dclose(dset));

  for (uint64_t iZone = 0; iZone < nZones; iZone++)
  {
    unsigned int elementType = static_cast<unsigned int>(cellType[iZone]);
    unsigned int zoneId = static_cast<unsigned int>(Id[iZone]);
    unsigned int firstIndex = static_cast<unsigned int>(minId[iZone]);
    unsigned int lastIndex = static_cast<unsigned int>(maxId[iZone]);
    // This next line should be uncommented following test with Fluent file
    // containing tree format (AMR)
    //// unsigned int child = static_cast<unsigned int>(childZoneId[iZone]);
    // next child and parent variable should be initialized correctly

    if (elementType == 0)
    {
      std::vector<int16_t> cellTypeData;
      hid_t group_ctype;
      uint64_t nSections;
      group_ctype = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1/cells/ctype", H5P_DEFAULT);
      if (group_ctype < 0)
      {
        throw std::runtime_error("Unable to open HDF group (GetCells ctype).");
      }
      attr = H5Aopen(group_ctype, "nSections", H5P_DEFAULT);
      if (attr < 0)
      {
        throw std::runtime_error("Unable to open HDF attribute (GetCells ctype).");
      }
      CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &nSections));
      CHECK_HDF(H5Aclose(attr));
      CHECK_HDF(H5Gclose(group_ctype));

      // Search for ctype section linked to the mixed zone
      uint64_t ctype_minId = 0, ctype_maxId = 0;
      for (uint64_t iSection = 0; iSection < nSections; iSection++)
      {
        int16_t ctype_elementType;
        std::string groupname =
          std::string("/meshes/1/cells/ctype/" + std::to_string(iSection + 1));
        group_ctype = H5Gopen(this->HDFImpl->FluentCaseFile, groupname.c_str(), H5P_DEFAULT);
        if (group_ctype < 0)
        {
          throw std::runtime_error("Unable to open HDF group (GetCells ctype section).");
        }

        attr = H5Aopen(group_ctype, "elementType", H5P_DEFAULT);
        if (attr < 0)
        {
          throw std::runtime_error("Unable to open HDF attribute (GetCells ctype section).");
        }
        CHECK_HDF(H5Aread(attr, H5T_NATIVE_INT16, &ctype_elementType));
        CHECK_HDF(H5Aclose(attr));
        attr = H5Aopen(group_ctype, "minId", H5P_DEFAULT);
        if (attr < 0)
        {
          throw std::runtime_error("Unable to open HDF attribute (GetCells ctype section).");
        }
        CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &ctype_minId));
        CHECK_HDF(H5Aclose(attr));
        attr = H5Aopen(group_ctype, "maxId", H5P_DEFAULT);
        if (attr < 0)
        {
          throw std::runtime_error("Unable to open HDF attribute (GetCells ctype section).");
        }
        CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &ctype_maxId));
        CHECK_HDF(H5Aclose(attr));

        if (static_cast<unsigned int>(ctype_elementType) == elementType &&
          static_cast<unsigned int>(ctype_minId) <= firstIndex &&
          static_cast<unsigned int>(ctype_maxId) >= lastIndex)
        {
          cellTypeData.resize(ctype_maxId - ctype_minId + 1);
          dset = H5Dopen(group_ctype, "cell-types", H5P_DEFAULT);
          if (dset < 0)
          {
            throw std::runtime_error("Unable to open HDF dataset (GetCells ctype section).");
          }
          CHECK_HDF(
            H5Dread(dset, H5T_NATIVE_INT16, H5S_ALL, H5S_ALL, H5P_DEFAULT, cellTypeData.data()));
          CHECK_HDF(H5Dclose(dset));
          CHECK_HDF(H5Gclose(group_ctype));
          break;
        }
        CHECK_HDF(H5Gclose(group_ctype));
      }

      if (!cellTypeData.empty())
      {
        for (unsigned int i = firstIndex; i <= lastIndex; i++)
        {
          this->Cells[i - 1].type = static_cast<unsigned int>(cellTypeData[i - ctype_minId]);
          this->Cells[i - 1].zone = zoneId;
          this->Cells[i - 1].parent = 0;
          this->Cells[i - 1].child = 0;
        }
      }
    }
    else
    {
      for (unsigned int i = firstIndex; i <= lastIndex; i++)
      {
        this->Cells[i - 1].type = elementType;
        this->Cells[i - 1].zone = zoneId;
        this->Cells[i - 1].parent = 0;
        this->Cells[i - 1].child = 0;
      }
    }
  }

  CHECK_HDF(H5Gclose(group));
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::GetFacesGlobal()
{
  hid_t group, attr;
  uint64_t firstIndex, lastIndex;
  group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1", H5P_DEFAULT);
  if (group < 0)
  {
    throw std::runtime_error("Unable to open HDF group (GetFacesGlobal).");
  }
  attr = H5Aopen(group, "faceOffset", H5P_DEFAULT);
  if (attr < 0)
  {
    throw std::runtime_error("Unable to open HDF attribute (GetFacesGlobal).");
  }
  CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &firstIndex));
  CHECK_HDF(H5Aclose(attr));
  attr = H5Aopen(group, "faceCount", H5P_DEFAULT);
  if (attr < 0)
  {
    throw std::runtime_error("Unable to open HDF attribute (GetFacesGlobal).");
  }
  CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &lastIndex));
  CHECK_HDF(H5Aclose(attr));
  CHECK_HDF(H5Gclose(group));
  this->Faces.resize(lastIndex);
  // Reserve one contiguous node pool for all faces.
  this->FaceNodePool.clear();
  this->FaceNodePool.reserve(static_cast<std::size_t>(lastIndex) * 4);
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::GetFaces()
{
  hid_t group, attr, dset;
  uint64_t nZones;
  group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1/faces/zoneTopology", H5P_DEFAULT);
  if (group < 0)
  {
    throw std::runtime_error("Unable to open HDF group (GetFaces).");
  }
  attr = H5Aopen(group, "nZones", H5P_DEFAULT);
  if (attr < 0)
  {
    throw std::runtime_error("Unable to open HDF attribute (GetFaces).");
  }
  CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &nZones));
  CHECK_HDF(H5Aclose(attr));

  std::vector<uint64_t> minId(nZones);
  dset = H5Dopen(group, "minId", H5P_DEFAULT);
  if (dset < 0)
  {
    throw std::runtime_error("Unable to open HDF dataset (GetFaces).");
  }
  CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, minId.data()));
  CHECK_HDF(H5Dclose(dset));

  std::vector<uint64_t> maxId(nZones);
  dset = H5Dopen(group, "maxId", H5P_DEFAULT);
  if (dset < 0)
  {
    throw std::runtime_error("Unable to open HDF dataset (GetFaces).");
  }
  CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, maxId.data()));
  CHECK_HDF(H5Dclose(dset));

  std::vector<int32_t> Id(nZones);
  dset = H5Dopen(group, "id", H5P_DEFAULT);
  if (dset < 0)
  {
    throw std::runtime_error("Unable to open HDF dataset (GetFaces).");
  }
  CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, Id.data()));
  CHECK_HDF(H5Dclose(dset));

  std::vector<std::string> zoneNames;
  dset = H5Dopen(group, "name", H5P_DEFAULT);
  if (dset >= 0)
  {
    hid_t spaceName = H5Dget_space(dset);
    hid_t dataTypeName = H5Dget_type(dset);
    std::size_t stringLength = H5Tget_size(dataTypeName);
    char* strchar = new char[stringLength];
    CHECK_HDF(H5Dread(dset, dataTypeName, H5S_ALL, H5S_ALL, H5P_DEFAULT, strchar));
    zoneNames = SplitFieldNames(std::string(strchar));
    delete[] strchar;
    CHECK_HDF(H5Tclose(dataTypeName));
    CHECK_HDF(H5Sclose(spaceName));
    CHECK_HDF(H5Dclose(dset));
  }

  std::vector<uint64_t> dimension(nZones);
  dset = H5Dopen(group, "dimension", H5P_DEFAULT);
  if (dset < 0)
  {
    throw std::runtime_error("Unable to open HDF dataset (GetFaces).");
  }
  CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, dimension.data()));
  CHECK_HDF(H5Dclose(dset));

  std::vector<int32_t> zoneT(nZones);
  dset = H5Dopen(group, "zoneType", H5P_DEFAULT);
  if (dset < 0)
  {
    throw std::runtime_error("Unable to open HDF dataset (GetFaces).");
  }
  CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT, H5S_ALL, H5S_ALL, H5P_DEFAULT, zoneT.data()));
  CHECK_HDF(H5Dclose(dset));

  std::vector<int32_t> faceT(nZones);
  dset = H5Dopen(group, "faceType", H5P_DEFAULT);
  if (dset < 0)
  {
    throw std::runtime_error("Unable to open HDF dataset (GetFaces).");
  }
  CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, faceT.data()));
  CHECK_HDF(H5Dclose(dset));

  std::vector<int32_t> childZoneId(nZones);
  dset = H5Dopen(group, "childZoneId", H5P_DEFAULT);
  if (dset < 0)
  {
    throw std::runtime_error("Unable to open HDF dataset (GetFaces).");
  }
  CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, childZoneId.data()));
  CHECK_HDF(H5Dclose(dset));

  std::vector<int32_t> shadowZoneId(nZones);
  dset = H5Dopen(group, "shadowZoneId", H5P_DEFAULT);
  if (dset < 0)
  {
    throw std::runtime_error("Unable to open HDF dataset (GetFaces).");
  }
  CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, shadowZoneId.data()));
  CHECK_HDF(H5Dclose(dset));

  std::vector<int32_t> flags(nZones);
  dset = H5Dopen(group, "flags", H5P_DEFAULT);
  if (dset < 0)
  {
    throw std::runtime_error("Unable to open HDF dataset (GetFaces).");
  }
  CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, flags.data()));
  CHECK_HDF(H5Dclose(dset));

  this->FaceZones.reserve(this->FaceZones.size() + static_cast<std::size_t>(nZones));
  for (uint64_t iZone = 0; iZone < nZones; iZone++)
  {
    unsigned int zoneId = static_cast<unsigned int>(Id[iZone]);
    unsigned int firstIndex = static_cast<unsigned int>(minId[iZone]);
    unsigned int lastIndex = static_cast<unsigned int>(maxId[iZone]);
    this->FaceZones.push_back({ iZone < zoneNames.size() ? zoneNames[iZone]
                                                         : std::string("zone_") + std::to_string(zoneId),
      static_cast<int>(zoneId), static_cast<vtkIdType>(firstIndex - 1),
      static_cast<vtkIdType>(lastIndex - 1) });
    // This next lines should be uncommented following test with Fluent file
    // containing tree format (AMR) and interface faces
    //// unsigned int child = static_cast<unsigned int>(childZoneId[iZone]);
    //// unsigned int shadow = static_cast<unsigned int>(shadowZoneId[iZone]);
    // next child, parent, periodicShadow variable should be initialized correctly

    for (unsigned int i = firstIndex; i <= lastIndex; i++)
    {
      this->Faces[i - 1].zone = zoneId;
      this->Faces[i - 1].periodicShadow = 0;
      this->Faces[i - 1].parent = 0;
      this->Faces[i - 1].child = 0;
      this->Faces[i - 1].interfaceFaceParent = 0;
      this->Faces[i - 1].ncgParent = 0;
      this->Faces[i - 1].ncgChild = 0;
      this->Faces[i - 1].interfaceFaceChild = 0;
    }
  }

  CHECK_HDF(H5Gclose(group));

  // FaceType
  uint64_t nSections;
  group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1/faces/nodes", H5P_DEFAULT);
  if (group < 0)
  {
    throw std::runtime_error("Unable to open HDF group (GetFaces nodes).");
  }
  attr = H5Aopen(group, "nSections", H5P_DEFAULT);
  if (attr < 0)
  {
    throw std::runtime_error("Unable to open HDF attribute (GetFaces nodes).");
  }
  CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &nSections));
  CHECK_HDF(H5Aclose(attr));
  CHECK_HDF(H5Gclose(group));

  for (uint64_t iSection = 0; iSection < nSections; iSection++)
  {
    uint64_t minId_fnodes, maxId_fnodes, nodes_size;
    std::string groupname = std::string("/meshes/1/faces/nodes/" + std::to_string(iSection + 1));
    group = H5Gopen(this->HDFImpl->FluentCaseFile, groupname.c_str(), H5P_DEFAULT);
    if (group < 0)
    {
      throw std::runtime_error("Unable to open HDF group (GetFaces nodes isection).");
    }

    attr = H5Aopen(group, "minId", H5P_DEFAULT);
    if (attr < 0)
    {
      throw std::runtime_error("Unable to open HDF attribute (GetFaces nodes isection).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &minId_fnodes));
    CHECK_HDF(H5Aclose(attr));
    attr = H5Aopen(group, "maxId", H5P_DEFAULT);
    if (attr < 0)
    {
      throw std::runtime_error("Unable to open HDF attribute (GetFaces nodes isection).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &maxId_fnodes));
    CHECK_HDF(H5Aclose(attr));

    std::vector<int16_t> nnodes_fnodes(maxId_fnodes - minId_fnodes + 1);
    dset = H5Dopen(group, "nnodes", H5P_DEFAULT);
    if (dset < 0)
    {
      throw std::runtime_error("Unable to open HDF dataset (GetFaces nodes isection).");
    }
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT16, H5S_ALL, H5S_ALL, H5P_DEFAULT, nnodes_fnodes.data()));
    CHECK_HDF(H5Dclose(dset));

    dset = H5Dopen(group, "nodes", H5P_DEFAULT);
    if (dset < 0)
    {
      throw std::runtime_error("Unable to open HDF dataset (GetFaces nodes isection).");
    }
    hid_t space = H5Dget_space(dset);
    hid_t ndims = H5Sget_simple_extent_ndims(space);
    if (ndims < 1)
    {
      throw std::runtime_error("Unable to open HDF group (GetFaces ndims < 1).");
    }
    std::vector<hsize_t> dims(ndims);
    CHECK_HDF(H5Sget_simple_extent_dims(space, dims.data(), nullptr));
    nodes_size = static_cast<uint64_t>(dims[0]);

    std::vector<uint32_t> nodes_fnodes(nodes_size);
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, nodes_fnodes.data()));
    CHECK_HDF(H5Dclose(dset));

    const std::size_t sectionOffset = this->FaceNodePool.size();
    this->FaceNodePool.resize(sectionOffset + static_cast<std::size_t>(nodes_size));
    int* const sectionNodePool = this->FaceNodePool.data() + sectionOffset;

    std::size_t localPtr = 0;
    for (unsigned int i = static_cast<unsigned int>(minId_fnodes);
         i <= static_cast<unsigned int>(maxId_fnodes); i++)
    {
      const int numberOfNodesInFace = static_cast<int>(nnodes_fnodes[i - minId_fnodes]);
      auto& face = this->Faces[i - 1];
      face.type = numberOfNodesInFace;
      face.nodeCount = numberOfNodesInFace;
      face.nodeOffset = static_cast<vtkIdType>(sectionOffset + localPtr);

      for (int k = 0; k < numberOfNodesInFace; k++)
      {
        const std::size_t srcIndex = localPtr++;
        sectionNodePool[srcIndex] = static_cast<int>(nodes_fnodes[srcIndex]) - 1;
      }
    }
    if (localPtr != static_cast<std::size_t>(nodes_size))
    {
      this->FaceNodePool.resize(sectionOffset + localPtr);
    }
    CHECK_HDF(H5Gclose(group));
  }

  // C0 C1
  std::vector<int> faceAdjacencyCount(this->Cells.size(), 0);
  group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1/faces/c0", H5P_DEFAULT);
  if (group < 0)
  {
    throw std::runtime_error("Unable to open HDF group (GetFaces c0).");
  }
  attr = H5Aopen(group, "nSections", H5P_DEFAULT);
  if (attr < 0)
  {
    throw std::runtime_error("Unable to open HDF attribute (GetFaces c0).");
  }
  CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &nSections));
  CHECK_HDF(H5Aclose(attr));
  for (uint64_t iSection = 0; iSection < nSections; iSection++)
  {
    uint64_t minc0, maxc0;

    dset = H5Dopen(group, std::to_string(iSection + 1).c_str(), H5P_DEFAULT);
    if (dset < 0)
    {
      throw std::runtime_error("Unable to open HDF dataset (GetFaces c0 iSection).");
    }
    attr = H5Aopen(dset, "minId", H5P_DEFAULT);
    if (attr < 0)
    {
      throw std::runtime_error("Unable to open HDF attribute (GetFaces c0 iSection).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &minc0));
    CHECK_HDF(H5Aclose(attr));
    attr = H5Aopen(dset, "maxId", H5P_DEFAULT);
    if (attr < 0)
    {
      throw std::runtime_error("Unable to open HDF attribute (GetFaces c0 iSection).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &maxc0));
    CHECK_HDF(H5Aclose(attr));

    std::vector<uint32_t> c0(maxc0 - minc0 + 1);
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, c0.data()));
    CHECK_HDF(H5Dclose(dset));

    for (unsigned int i = static_cast<unsigned int>(minc0); i <= static_cast<unsigned int>(maxc0);
         i++)
    {
      this->Faces[i - 1].c0 = static_cast<int>(c0[i - minc0]) - 1;
      if (this->Faces[i - 1].c0 >= 0)
      {
        ++faceAdjacencyCount[static_cast<std::size_t>(this->Faces[i - 1].c0)];
      }
    }
  }
  CHECK_HDF(H5Gclose(group));

  group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1/faces/c1", H5P_DEFAULT);
  if (group < 0)
  {
    throw std::runtime_error("Unable to open HDF group (GetFaces c1).");
  }
  attr = H5Aopen(group, "nSections", H5P_DEFAULT);
  if (attr < 0)
  {
    throw std::runtime_error("Unable to open HDF attribute (GetFaces c1).");
  }
  CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &nSections));
  CHECK_HDF(H5Aclose(attr));
  for (std::size_t i = 0; i < this->Faces.size(); i++)
  {
    this->Faces[i].c1 = -1;
  }
  for (uint64_t iSection = 0; iSection < nSections; iSection++)
  {
    uint64_t minc1, maxc1;

    dset = H5Dopen(group, std::to_string(iSection + 1).c_str(), H5P_DEFAULT);
    if (dset < 0)
    {
      throw std::runtime_error("Unable to open HDF dataset (GetFaces c1 iSection).");
    }
    attr = H5Aopen(dset, "minId", H5P_DEFAULT);
    if (attr < 0)
    {
      throw std::runtime_error("Unable to open HDF attribute (GetFaces c1 iSection).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &minc1));
    CHECK_HDF(H5Aclose(attr));
    attr = H5Aopen(dset, "maxId", H5P_DEFAULT);
    if (attr < 0)
    {
      throw std::runtime_error("Unable to open HDF attribute (GetFaces c1 iSection).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &maxc1));
    CHECK_HDF(H5Aclose(attr));

    std::vector<uint32_t> c1(maxc1 - minc1 + 1);
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, c1.data()));
    CHECK_HDF(H5Dclose(dset));

    for (unsigned int i = static_cast<unsigned int>(minc1); i <= static_cast<unsigned int>(maxc1);
         i++)
    {
      this->Faces[i - 1].c1 = static_cast<int>(c1[i - minc1]) - 1;
      if (this->Faces[i - 1].c1 >= 0)
      {
        ++faceAdjacencyCount[static_cast<std::size_t>(this->Faces[i - 1].c1)];
      }
    }
  }

  CHECK_HDF(H5Gclose(group));

  for (std::size_t cellIndex = 0; cellIndex < this->Cells.size(); ++cellIndex)
  {
    auto& facesOfCell = this->Cells[cellIndex].faces;
    facesOfCell.clear();
    facesOfCell.reserve(static_cast<std::size_t>(faceAdjacencyCount[cellIndex]));
  }

  for (std::size_t faceIndex = 0; faceIndex < this->Faces.size(); ++faceIndex)
  {
    const auto& face = this->Faces[faceIndex];
    if (face.c0 >= 0)
    {
      this->Cells[static_cast<std::size_t>(face.c0)].faces.push_back(static_cast<int>(faceIndex));
    }
    if (face.c1 >= 0)
    {
      this->Cells[static_cast<std::size_t>(face.c1)].faces.push_back(static_cast<int>(faceIndex));
    }
  }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::GetPeriodicShadowFaces()
{
  // TODO: Periodic shadow faces read should be added following test with Fluent file
  // containing periodic faces
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::GetCellOverset()
{
  herr_t s1 = H5Gget_objinfo(this->HDFImpl->FluentCaseFile, "/special/Overset_DCI", false, nullptr);
  if (s1 == 0)
  {
    vtkWarningMacro("The overset layout of this CFF file cannot be displayed by this reader.");
    // TODO: Overset cells read should be added following test with Fluent file
    // containing overset cell zone
    // This function can read the overset structure but Ansys Fluent does not
    // give any explanation about the structure of the overset data.
    /*
    hid_t group, attr, dset;
    uint64_t nSections;
    group = H5Gopen(this->HDFImpl->FluentCaseFile, "/special/Overset_DCI/cells", H5P_DEFAULT);
    if (group < 0)
    {
      throw std::runtime_error("Unable to open HDF group (GetCellOverset).");
    }
    dset = H5Dopen(group, "topology", H5P_DEFAULT);
    if (dset < 0)
    {
      throw std::runtime_error("Unable to open HDF dataset (GetCellOverset).");
    }
    hid_t space = H5Dget_space(dset);
    hid_t ndims = H5Sget_simple_extent_ndims(space);
    if (ndims < 1)
    {
      throw std::runtime_error("Unable to open HDF group (GetCellOverset ndims < 1).");
    }
    std::vector<hsize_t> dims(ndims);
    CHECK_HDF(H5Sget_simple_extent_dims(space, dims.data(), nullptr));
    nSections = static_cast<uint64_t>(dims[0]);

    std::vector<int32_t> topology(nSections);
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT,
topology.data()); CHECK_HDF(H5Dclose(dset));

    for (int iSection = 0; iSection < nSections; iSection++)
    {
      hid_t groupTopo = H5Gopen(group, std::to_string(topology[iSection]).c_str(), H5P_DEFAULT);
      if (groupTopo < 0)
      {
        throw std::runtime_error("Unable to open HDF group (GetCellOverset topology).");
      }

      uint64_t minId, maxId;
      attr = H5Aopen(groupTopo, "minId", H5P_DEFAULT);
      if (attr < 0)
      {
        throw std::runtime_error("Unable to open HDF attribute (GetCellOverset topology).");
      }
      CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &minId));
      CHECK_HDF(H5Aclose(attr));
      attr = H5Aopen(groupTopo, "maxId", H5P_DEFAULT);
      if (attr < 0)
      {
        throw std::runtime_error("Unable to open HDF attribute (GetCellOverset topology).");
      }
      CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &maxId));
      CHECK_HDF(H5Aclose(attr));

      std::vector<int32_t> ndata(maxId - minId + 1);
      dset = H5Dopen(groupTopo, "ndata", H5P_DEFAULT);
      if (dset < 0)
      {
        throw std::runtime_error("Unable to open HDF dataset (GetCellOverset topology).");
      }
      CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT32, H5S_ALL, H5S_ALL, H5P_DEFAULT,
ndata.data()));
      CHECK_HDF(H5Dclose(dset));

      for (unsigned int i = static_cast<unsigned int>(minId); i <= static_cast<unsigned int>(maxId);
i++)
      {
        if (ndata[i - minId] != 4)
          this->Cells[i - 1].overset = 1;
      }

      dset = H5Dopen(groupTopo, "data", H5P_DEFAULT);
      if (dset < 0)
      {
        throw std::runtime_error("Unable to open HDF dataset (GetCellOverset topology).");
      }
      uint64_t size_data;
      hid_t space = H5Dget_space(dset);
      hid_t ndims = H5Sget_simple_extent_ndims(space);
      std::vector<hsize_t> dims(ndims);
      CHECK_HDF(H5Sget_simple_extent_dims(space, dims.data(), nullptr));
      size_data = static_cast<uint64_t>(dims[0]);
      std::vector<int8_t> data(size_data);
      CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT8, H5S_ALL, H5S_ALL, H5P_DEFAULT,
data.data()));
      CHECK_HDF(H5Dclose(dset));

      CHECK_HDF(H5Dclose(dset));
      CHECK_HDF(H5Gclose(groupTopo));
    }

    CHECK_HDF(H5Gclose(group));*/
  }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::GetCellTree()
{
  herr_t s1 = H5Gget_objinfo(this->HDFImpl->FluentCaseFile, "/meshes/1/cells/tree", false, nullptr);
  if (s1 == 0)
  {
    hid_t group, attr, dset;
    uint64_t minId, maxId;
    group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1/cells/tree/1", H5P_DEFAULT);
    if (group < 0)
    {
      throw std::runtime_error("Unable to open HDF group (GetCellTree).");
    }
    attr = H5Aopen(group, "minId", H5P_DEFAULT);
    if (attr < 0)
    {
      throw std::runtime_error("Unable to open HDF attribute (GetCellTree).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &minId));
    CHECK_HDF(H5Aclose(attr));
    attr = H5Aopen(group, "maxId", H5P_DEFAULT);
    if (attr < 0)
    {
      throw std::runtime_error("Unable to open HDF attribute (GetCellTree).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &maxId));
    CHECK_HDF(H5Aclose(attr));

    std::vector<int16_t> nkids(maxId - minId + 1);
    dset = H5Dopen(group, "nkids", H5P_DEFAULT);
    if (dset < 0)
    {
      throw std::runtime_error("Unable to open HDF dataset (GetCellTree).");
    }
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT16, H5S_ALL, H5S_ALL, H5P_DEFAULT, nkids.data()));
    CHECK_HDF(H5Dclose(dset));

    uint64_t kids_size;
    dset = H5Dopen(group, "kids", H5P_DEFAULT);
    if (dset < 0)
    {
      throw std::runtime_error("Unable to open HDF dataset (GetCellTree).");
    }
    hid_t space = H5Dget_space(dset);
    hid_t ndims = H5Sget_simple_extent_ndims(space);
    if (ndims < 1)
    {
      throw std::runtime_error("Unable to open HDF group (GetCellTree ndims < 1).");
    }
    std::vector<hsize_t> dims(ndims);
    CHECK_HDF(H5Sget_simple_extent_dims(space, dims.data(), nullptr));
    kids_size = static_cast<uint64_t>(dims[0]);

    std::vector<uint32_t> kids(kids_size);
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, kids.data()));
    CHECK_HDF(H5Dclose(dset));

    uint64_t ptr = 0;
    for (unsigned int i = static_cast<unsigned int>(minId); i <= static_cast<unsigned int>(maxId);
         i++)
    {
      this->Cells[i - 1].parent = 1;
      int numberOfKids = static_cast<int>(nkids[i - minId]);
      this->Cells[i - 1].childId.resize(numberOfKids);
      for (int j = 0; j < numberOfKids; j++)
      {
        this->Cells[kids[ptr] - 1].child = 1;
        this->Cells[i - 1].childId[j] = kids[ptr] - 1;
        ptr++;
      }
    }

    CHECK_HDF(H5Gclose(group));
  }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::GetFaceTree()
{
  herr_t s1 = H5Gget_objinfo(this->HDFImpl->FluentCaseFile, "/meshes/1/faces/tree", false, nullptr);
  if (s1 == 0)
  {
    hid_t group, attr, dset;
    uint64_t minId, maxId;
    group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1/faces/tree/1", H5P_DEFAULT);
    if (group < 0)
    {
      throw std::runtime_error("Unable to open HDF group (GetFaceTree).");
    }
    attr = H5Aopen(group, "minId", H5P_DEFAULT);
    if (attr < 0)
    {
      throw std::runtime_error("Unable to open HDF attribute (GetFaceTree).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &minId));
    CHECK_HDF(H5Aclose(attr));
    attr = H5Aopen(group, "maxId", H5P_DEFAULT);
    if (attr < 0)
    {
      throw std::runtime_error("Unable to open HDF attribute (GetFaceTree).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &maxId));
    CHECK_HDF(H5Aclose(attr));

    std::vector<int16_t> nkids(maxId - minId + 1);
    dset = H5Dopen(group, "nkids", H5P_DEFAULT);
    if (dset < 0)
    {
      throw std::runtime_error("Unable to open HDF dataset (GetFaceTree).");
    }
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_INT16, H5S_ALL, H5S_ALL, H5P_DEFAULT, nkids.data()));
    CHECK_HDF(H5Dclose(dset));

    uint64_t kids_size;
    dset = H5Dopen(group, "kids", H5P_DEFAULT);
    if (dset < 0)
    {
      throw std::runtime_error("Unable to open HDF dataset (GetFaceTree).");
    }
    hid_t space = H5Dget_space(dset);
    hid_t ndims = H5Sget_simple_extent_ndims(space);
    if (ndims < 1)
    {
      throw std::runtime_error("Unable to open HDF group (GetFaceTree ndims < 1).");
    }
    std::vector<hsize_t> dims(ndims);
    CHECK_HDF(H5Sget_simple_extent_dims(space, dims.data(), nullptr));
    kids_size = static_cast<uint64_t>(dims[0]);

    std::vector<uint32_t> kids(kids_size);
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT32, H5S_ALL, H5S_ALL, H5P_DEFAULT, kids.data()));
    CHECK_HDF(H5Dclose(dset));

    uint64_t ptr = 0;
    for (unsigned int i = static_cast<unsigned int>(minId); i <= static_cast<unsigned int>(maxId);
         i++)
    {
      this->Faces[i - 1].parent = 1;
      int numberOfKids = static_cast<int>(nkids[i - minId]);
      for (int j = 0; j < numberOfKids; j++)
      {
        this->Faces[kids[ptr] - 1].child = 1;
        ptr++;
      }
    }

    CHECK_HDF(H5Gclose(group));
  }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::GetInterfaceFaceParents()
{
  herr_t s1 =
    H5Gget_objinfo(this->HDFImpl->FluentCaseFile, "/meshes/1/faces/interface", false, nullptr);
  if (s1 == 0)
  {
    hid_t group, attr, dset;
    uint64_t nData, nZones;
    group = H5Gopen(this->HDFImpl->FluentCaseFile, "/meshes/1/faces/interface", H5P_DEFAULT);
    if (group < 0)
    {
      throw std::runtime_error("Unable to open HDF group (GetInterfaceFaceParents).");
    }
    attr = H5Aopen(group, "nData", H5P_DEFAULT);
    if (attr < 0)
    {
      throw std::runtime_error("Unable to open HDF attribute (GetInterfaceFaceParents).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &nData));
    CHECK_HDF(H5Aclose(attr));
    attr = H5Aopen(group, "nZones", H5P_DEFAULT);
    if (attr < 0)
    {
      throw std::runtime_error("Unable to open HDF attribute (GetInterfaceFaceParents).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &nZones));
    CHECK_HDF(H5Aclose(attr));

    std::vector<uint64_t> nciTopology(nData * nZones);
    dset = H5Dopen(group, "nciTopology", H5P_DEFAULT);
    if (dset < 0)
    {
      throw std::runtime_error("Unable to open HDF dataset (GetInterfaceFaceParents).");
    }
    CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, nciTopology.data()));
    CHECK_HDF(H5Dclose(dset));

    for (uint64_t iZone = 0; iZone < nZones; iZone++)
    {
      int zoneId = static_cast<int>(nciTopology[iZone * nData]);
      int minId = static_cast<int>(nciTopology[iZone * nData + 1]);
      int maxId = static_cast<int>(nciTopology[iZone * nData + 2]);

      hid_t group_int = H5Gopen(group, std::to_string(zoneId).c_str(), H5P_DEFAULT);
      if (group_int < 0)
      {
        throw std::runtime_error("Unable to open HDF group (GetInterfaceFaceParents topology).");
      }
      std::vector<uint64_t> pf0(maxId - minId + 1);
      std::vector<uint64_t> pf1(maxId - minId + 1);
      dset = H5Dopen(group_int, "pf0", H5P_DEFAULT);
      if (dset < 0)
      {
        throw std::runtime_error("Unable to open HDF dataset (GetInterfaceFaceParents topology).");
      }
      CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, pf0.data()));
      CHECK_HDF(H5Dclose(dset));
      dset = H5Dopen(group_int, "pf1", H5P_DEFAULT);
      if (dset < 0)
      {
        throw std::runtime_error("Unable to open HDF dataset (GetInterfaceFaceParents topology).");
      }
      CHECK_HDF(H5Dread(dset, H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT, pf1.data()));
      CHECK_HDF(H5Dclose(dset));

      for (unsigned int i = static_cast<unsigned int>(minId); i <= static_cast<unsigned int>(maxId);
           i++)
      {
        unsigned int parentId0 = static_cast<unsigned int>(pf0[i - minId]);
        unsigned int parentId1 = static_cast<unsigned int>(pf1[i - minId]);

        this->Faces[parentId0 - 1].interfaceFaceParent = 1;
        this->Faces[parentId1 - 1].interfaceFaceParent = 1;
        this->Faces[i - 1].interfaceFaceChild = 1;
      }
      CHECK_HDF(H5Gclose(group_int));
    }

    CHECK_HDF(H5Gclose(group));
  }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::GetNonconformalGridInterfaceFaceInformation()
{
  // TODO: Non conformal faces read should be added following test with Fluent file
  // containing interface faces
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::CleanCells()
{

  std::vector<int> t;
  for (auto& cell : this->Cells)
  {

    if (((cell.type == 1) && (cell.faces.size() != 3)) ||
      ((cell.type == 2) && (cell.faces.size() != 4)) ||
      ((cell.type == 3) && (cell.faces.size() != 4)) ||
      ((cell.type == 4) && (cell.faces.size() != 6)) ||
      ((cell.type == 5) && (cell.faces.size() != 5)) ||
      ((cell.type == 6) && (cell.faces.size() != 5)))
    {

      // Copy faces
      t.clear();
      for (std::size_t j = 0; j < cell.faces.size(); j++)
      {
        t.push_back(cell.faces[j]);
      }

      // Clear Faces
      cell.faces.clear();

      // Copy the faces that are not flagged back into the cell
      for (std::size_t j = 0; j < t.size(); j++)
      {
        if ((this->Faces[t[j]].child == 0) && (this->Faces[t[j]].ncgChild == 0) &&
          (this->Faces[t[j]].interfaceFaceChild == 0))
        {
          cell.faces.push_back(t[j]);
        }
      }
    }
  }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::PopulateCellTree()
{
  for (std::size_t cellIndex = 0; cellIndex < this->Cells.size(); ++cellIndex)
  {
    const auto& cell = this->Cells[cellIndex];
    // If cell is parent cell -> interpolate data from children
    if (cell.parent == 1)
    {
      for (auto& dataChunk : this->CellDataChunks)
      {
        for (std::size_t k = 0; k < dataChunk.dim; k++)
        {
          double data = 0.0;
          int ncell = 0;
          for (std::size_t j = 0; j < cell.childId.size(); j++)
          {
            if (this->Cells[cell.childId[j]].parent == 0)
            {
              data += dataChunk.dataVector[k + dataChunk.dim * cell.childId[j]];
              ncell++;
            }
          }
          const std::size_t offset = cellIndex * dataChunk.dim + k;
          if (offset < dataChunk.dataVector.size())
          {
            dataChunk.dataVector[offset] =
              (ncell != 0 ? data / static_cast<double>(ncell) : 0.0);
          }
        }
      }
    }
  }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::PopulateCellNodes()
{
  this->CellNodePool.clear();
  this->CellNodeOffsetPool.clear();
  this->CellUniqueNodePool.clear();
  std::size_t cellNodeEstimate = 0;
  std::size_t polyOffsetEstimate = 0;
  std::size_t polyUniqueEstimate = 0;
  for (const auto& cell : this->Cells)
  {
    switch (cell.type)
    {
      case 1:
        cellNodeEstimate += 3;
        break;
      case 2:
        cellNodeEstimate += 4;
        break;
      case 3:
        cellNodeEstimate += 4;
        break;
      case 4:
        cellNodeEstimate += 8;
        break;
      case 5:
        cellNodeEstimate += 5;
        break;
      case 6:
        cellNodeEstimate += 6;
        break;
      case 7:
        polyOffsetEstimate += cell.faces.size() + 1;
        polyUniqueEstimate += cell.faces.size() * 2;
        for (int faceId : cell.faces)
        {
          if (faceId < 0 || static_cast<std::size_t>(faceId) >= this->Faces.size())
          {
            continue;
          }
          cellNodeEstimate +=
            static_cast<std::size_t>(this->Faces[static_cast<std::size_t>(faceId)].nodeCount);
        }
        break;
      default:
        break;
    }
  }
  this->CellNodePool.reserve(cellNodeEstimate);
  this->CellNodeOffsetPool.reserve(polyOffsetEstimate);
  this->CellUniqueNodePool.reserve(polyUniqueEstimate);
  std::vector<int> pointVisitStamp;
  pointVisitStamp.resize(static_cast<std::size_t>(this->Points->GetNumberOfPoints()), -1);
  int visitToken = 0;

  for (std::size_t i = 0; i < this->Cells.size(); i++)
  {
    auto& cell = this->Cells[i];
    cell.nodePoolOffset = 0;
    cell.nodePoolCount = 0;
    cell.nodeOffsetPoolOffset = 0;
    cell.nodeOffsetPoolCount = 0;
    cell.uniqueNodePoolOffset = 0;
    cell.uniqueNodePoolCount = 0;

    const vtkIdType id = static_cast<vtkIdType>(i);
    switch (cell.type)
    {
      case 1: // Triangle
        this->PopulateTriangleCell(id);
        break;

      case 2: // Tetrahedron
        this->PopulateTetraCell(id);
        break;

      case 3: // Quadrilateral
        this->PopulateQuadCell(id);
        break;

      case 4: // Hexahedral
        this->PopulateHexahedronCell(id);
        break;

      case 5: // Pyramid
        this->PopulatePyramidCell(id);
        break;

      case 6: // Wedge
        this->PopulateWedgeCell(id);
        break;

      case 7: // Polyhedron
        this->PopulatePolyhedronCell(id);
        break;
    }

    if (cell.type != 7 && !cell.nodes.empty())
    {
      cell.nodePoolOffset = static_cast<vtkIdType>(this->CellNodePool.size());
      cell.nodePoolCount = static_cast<int>(cell.nodes.size());
      this->CellNodePool.insert(this->CellNodePool.end(), cell.nodes.begin(), cell.nodes.end());
    }
    else if (cell.type == 7 && cell.nodePoolCount > 0)
    {
      cell.uniqueNodePoolOffset = static_cast<vtkIdType>(this->CellUniqueNodePool.size());
      ++visitToken;
      if (visitToken == std::numeric_limits<int>::max())
      {
        std::fill(pointVisitStamp.begin(), pointVisitStamp.end(), -1);
        visitToken = 0;
      }
      const std::size_t nodeOffset = static_cast<std::size_t>(cell.nodePoolOffset);
      const std::size_t nodeCount = static_cast<std::size_t>(cell.nodePoolCount);
      for (std::size_t nodeIdx = 0; nodeIdx < nodeCount; ++nodeIdx)
      {
        const int nodeId = this->CellNodePool[nodeOffset + nodeIdx];
        if (nodeId >= 0 && static_cast<std::size_t>(nodeId) < pointVisitStamp.size() &&
          pointVisitStamp[static_cast<std::size_t>(nodeId)] != visitToken)
        {
          pointVisitStamp[static_cast<std::size_t>(nodeId)] = visitToken;
          this->CellUniqueNodePool.push_back(static_cast<vtkIdType>(nodeId));
        }
      }
      cell.uniqueNodePoolCount =
        static_cast<int>(this->CellUniqueNodePool.size() -
          static_cast<std::size_t>(cell.uniqueNodePoolOffset));
    }
  }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::PopulateTriangleCell(int i)
{
  this->Cells[i].nodes.resize(3);
  const int face0Id = this->Cells[i].faces[0];
  const int face1Id = this->Cells[i].faces[1];
  const auto& face0 = this->Faces[static_cast<std::size_t>(face0Id)];
  const auto& face1 = this->Faces[static_cast<std::size_t>(face1Id)];
  const int* face0Nodes = this->FaceNodePool.data() + static_cast<std::size_t>(face0.nodeOffset);
  const int* face1Nodes = this->FaceNodePool.data() + static_cast<std::size_t>(face1.nodeOffset);
  if (face0.c0 == i)
  {
    this->Cells[i].nodes[0] = face0Nodes[0];
    this->Cells[i].nodes[1] = face0Nodes[1];
  }
  else
  {
    this->Cells[i].nodes[1] = face0Nodes[0];
    this->Cells[i].nodes[0] = face0Nodes[1];
  }

  if (face1Nodes[0] != this->Cells[i].nodes[0] && face1Nodes[0] != this->Cells[i].nodes[1])
  {
    this->Cells[i].nodes[2] = face1Nodes[0];
  }
  else
  {
    this->Cells[i].nodes[2] = face1Nodes[1];
  }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::PopulateTetraCell(int i)
{
  this->Cells[i].nodes.resize(4);
  const int face0Id = this->Cells[i].faces[0];
  const int face1Id = this->Cells[i].faces[1];
  const auto& face0 = this->Faces[static_cast<std::size_t>(face0Id)];
  const auto& face1 = this->Faces[static_cast<std::size_t>(face1Id)];
  const int* face0Nodes = this->FaceNodePool.data() + static_cast<std::size_t>(face0.nodeOffset);
  const int* face1Nodes = this->FaceNodePool.data() + static_cast<std::size_t>(face1.nodeOffset);

  if (face0.c0 == i)
  {
    this->Cells[i].nodes[0] = face0Nodes[0];
    this->Cells[i].nodes[1] = face0Nodes[1];
    this->Cells[i].nodes[2] = face0Nodes[2];
  }
  else
  {
    this->Cells[i].nodes[2] = face0Nodes[0];
    this->Cells[i].nodes[1] = face0Nodes[1];
    this->Cells[i].nodes[0] = face0Nodes[2];
  }

  if (face1Nodes[0] != this->Cells[i].nodes[0] && face1Nodes[0] != this->Cells[i].nodes[1] &&
    face1Nodes[0] != this->Cells[i].nodes[2])
  {
    this->Cells[i].nodes[3] = face1Nodes[0];
  }
  else if (face1Nodes[1] != this->Cells[i].nodes[0] && face1Nodes[1] != this->Cells[i].nodes[1] &&
    face1Nodes[1] != this->Cells[i].nodes[2])
  {
    this->Cells[i].nodes[3] = face1Nodes[1];
  }
  else
  {
    this->Cells[i].nodes[3] = face1Nodes[2];
  }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::PopulateQuadCell(int i)
{
  this->Cells[i].nodes.resize(4);
  const int face0Id = this->Cells[i].faces[0];
  const int face1Id = this->Cells[i].faces[1];
  const int face2Id = this->Cells[i].faces[2];
  const int face3Id = this->Cells[i].faces[3];
  const auto& face0 = this->Faces[static_cast<std::size_t>(face0Id)];
  const auto& face1 = this->Faces[static_cast<std::size_t>(face1Id)];
  const auto& face2 = this->Faces[static_cast<std::size_t>(face2Id)];
  const auto& face3 = this->Faces[static_cast<std::size_t>(face3Id)];
  const int* face0Nodes = this->FaceNodePool.data() + static_cast<std::size_t>(face0.nodeOffset);
  const int* face1Nodes = this->FaceNodePool.data() + static_cast<std::size_t>(face1.nodeOffset);
  const int* face2Nodes = this->FaceNodePool.data() + static_cast<std::size_t>(face2.nodeOffset);
  const int* face3Nodes = this->FaceNodePool.data() + static_cast<std::size_t>(face3.nodeOffset);

  if (face0.c0 == i)
  {
    this->Cells[i].nodes[0] = face0Nodes[0];
    this->Cells[i].nodes[1] = face0Nodes[1];
  }
  else
  {
    this->Cells[i].nodes[1] = face0Nodes[0];
    this->Cells[i].nodes[0] = face0Nodes[1];
  }

  if ((face1Nodes[0] != this->Cells[i].nodes[0] && face1Nodes[0] != this->Cells[i].nodes[1]) &&
    (face1Nodes[1] != this->Cells[i].nodes[0] && face1Nodes[1] != this->Cells[i].nodes[1]))
  {
    if (face1.c0 == i)
    {
      this->Cells[i].nodes[2] = face1Nodes[0];
      this->Cells[i].nodes[3] = face1Nodes[1];
    }
    else
    {
      this->Cells[i].nodes[3] = face1Nodes[0];
      this->Cells[i].nodes[2] = face1Nodes[1];
    }
  }
  else if ((face2Nodes[0] != this->Cells[i].nodes[0] && face2Nodes[0] != this->Cells[i].nodes[1]) &&
    (face2Nodes[1] != this->Cells[i].nodes[0] && face2Nodes[1] != this->Cells[i].nodes[1]))
  {
    if (face2.c0 == i)
    {
      this->Cells[i].nodes[2] = face2Nodes[0];
      this->Cells[i].nodes[3] = face2Nodes[1];
    }
    else
    {
      this->Cells[i].nodes[3] = face2Nodes[0];
      this->Cells[i].nodes[2] = face2Nodes[1];
    }
  }
  else
  {
    if (face3.c0 == i)
    {
      this->Cells[i].nodes[2] = face3Nodes[0];
      this->Cells[i].nodes[3] = face3Nodes[1];
    }
    else
    {
      this->Cells[i].nodes[3] = face3Nodes[0];
      this->Cells[i].nodes[2] = face3Nodes[1];
    }
  }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::PopulateHexahedronCell(int i)
{
  this->Cells[i].nodes.resize(8);

  // Throw error when number of face of hexahedron cell is below 4.
  // Number of face should be 6 but you can find the 8 corner points with at least 4 faces.
  if (this->Cells[i].faces.size() < 4)
  {
    throw std::runtime_error("Some cells of the domain are incompatible with this reader.");
  }

  const int baseFaceId = this->Cells[i].faces[0];
  const auto& baseFace = this->Faces[static_cast<std::size_t>(baseFaceId)];
  const int* baseNodes =
    this->FaceNodePool.data() + static_cast<std::size_t>(baseFace.nodeOffset);

  if (baseFace.c0 == i)
  {
    for (int j = 0; j < 4; j++)
    {
      this->Cells[i].nodes[j] = baseNodes[j];
    }
  }
  else
  {
    for (int j = 3; j >= 0; j--)
    {
      this->Cells[i].nodes[3 - j] = baseNodes[j];
    }
  }

  //  Look for opposite face of hexahedron
  for (std::size_t j = 1; j < this->Cells[i].faces.size(); j++)
  {
    const int faceId = this->Cells[i].faces[j];
    const auto& face = this->Faces[static_cast<std::size_t>(faceId)];
    const int* faceNodes = this->FaceNodePool.data() + static_cast<std::size_t>(face.nodeOffset);
    int flag = 0;
    for (int k = 0; k < 4; k++)
    {
      if ((this->Cells[i].nodes[0] == faceNodes[k]) || (this->Cells[i].nodes[1] == faceNodes[k]) ||
        (this->Cells[i].nodes[2] == faceNodes[k]) || (this->Cells[i].nodes[3] == faceNodes[k]))
      {
        flag = 1;
      }
    }
    if (flag == 0)
    {
      if (face.c1 == i)
      {
        for (int k = 4; k < 8; k++)
        {
          this->Cells[i].nodes[k] = faceNodes[k - 4];
        }
      }
      else
      {
        for (int k = 7; k >= 4; k--)
        {
          this->Cells[i].nodes[k] = faceNodes[7 - k];
        }
      }
    }
  }

  //  Find the face with points 0 and 1 in them.
  int f01[4] = { -1, -1, -1, -1 };
  for (std::size_t j = 1; j < this->Cells[i].faces.size(); j++)
  {
    const int faceId = this->Cells[i].faces[j];
    const auto& face = this->Faces[static_cast<std::size_t>(faceId)];
    const int* faceNodes = this->FaceNodePool.data() + static_cast<std::size_t>(face.nodeOffset);
    int flag0 = 0;
    int flag1 = 0;
    for (int k = 0; k < 4; k++)
    {
      if (this->Cells[i].nodes[0] == faceNodes[k])
      {
        flag0 = 1;
      }
      if (this->Cells[i].nodes[1] == faceNodes[k])
      {
        flag1 = 1;
      }
    }
    if ((flag0 == 1) && (flag1 == 1))
    {
      if (face.c0 == i)
      {
        for (int k = 0; k < 4; k++)
        {
          f01[k] = faceNodes[k];
        }
      }
      else
      {
        for (int k = 3; k >= 0; k--)
        {
          f01[k] = faceNodes[k];
        }
      }
    }
  }

  //  Find the face with points 0 and 3 in them.
  int f03[4] = { -1, -1, -1, -1 };
  for (std::size_t j = 1; j < this->Cells[i].faces.size(); j++)
  {
    const int faceId = this->Cells[i].faces[j];
    const auto& face = this->Faces[static_cast<std::size_t>(faceId)];
    const int* faceNodes = this->FaceNodePool.data() + static_cast<std::size_t>(face.nodeOffset);
    int flag0 = 0;
    int flag1 = 0;
    for (int k = 0; k < 4; k++)
    {
      if (this->Cells[i].nodes[0] == faceNodes[k])
      {
        flag0 = 1;
      }
      if (this->Cells[i].nodes[3] == faceNodes[k])
      {
        flag1 = 1;
      }
    }

    if ((flag0 == 1) && (flag1 == 1))
    {
      if (face.c0 == i)
      {
        for (int k = 0; k < 4; k++)
        {
          f03[k] = faceNodes[k];
        }
      }
      else
      {
        for (int k = 3; k >= 0; k--)
        {
          f03[k] = faceNodes[k];
        }
      }
    }
  }

  // What point is in f01 and f03 besides 0 ... this is point 4
  int p4 = 0;
  for (int k = 0; k < 4; k++)
  {
    if (f01[k] != this->Cells[i].nodes[0])
    {
      for (int n = 0; n < 4; n++)
      {
        if (f01[k] == f03[n])
        {
          p4 = f01[k];
        }
      }
    }
  }

  // Since we know point 4 now we check to see if points
  //  4, 5, 6, and 7 are in the correct positions.
  int t[8];
  t[4] = this->Cells[i].nodes[4];
  t[5] = this->Cells[i].nodes[5];
  t[6] = this->Cells[i].nodes[6];
  t[7] = this->Cells[i].nodes[7];
  if (p4 == this->Cells[i].nodes[5])
  {
    this->Cells[i].nodes[5] = t[6];
    this->Cells[i].nodes[6] = t[7];
    this->Cells[i].nodes[7] = t[4];
    this->Cells[i].nodes[4] = t[5];
  }
  else if (p4 == Cells[i].nodes[6])
  {
    this->Cells[i].nodes[5] = t[7];
    this->Cells[i].nodes[6] = t[4];
    this->Cells[i].nodes[7] = t[5];
    this->Cells[i].nodes[4] = t[6];
  }
  else if (p4 == Cells[i].nodes[7])
  {
    this->Cells[i].nodes[5] = t[4];
    this->Cells[i].nodes[6] = t[5];
    this->Cells[i].nodes[7] = t[6];
    this->Cells[i].nodes[4] = t[7];
  }
  // else point 4 was lined up so everything was correct.
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::PopulatePyramidCell(int i)
{
  this->Cells[i].nodes.resize(5);
  //  The quad face will be the base of the pyramid
  for (std::size_t j = 0; j < this->Cells[i].faces.size(); j++)
  {
    const int faceId = this->Cells[i].faces[j];
    const auto& face = this->Faces[static_cast<std::size_t>(faceId)];
    const int* faceNodes = this->FaceNodePool.data() + static_cast<std::size_t>(face.nodeOffset);
    if (face.nodeCount == 4)
    {
      if (face.c0 == i)
      {
        for (int k = 0; k < 4; k++)
        {
          this->Cells[i].nodes[k] = faceNodes[k];
        }
      }
      else
      {
        for (int k = 0; k < 4; k++)
        {
          this->Cells[i].nodes[3 - k] = faceNodes[k];
        }
      }
    }
  }

  // Just need to find point 4
  for (std::size_t j = 0; j < this->Cells[i].faces.size(); j++)
  {
    const int faceId = this->Cells[i].faces[j];
    const auto& face = this->Faces[static_cast<std::size_t>(faceId)];
    const int* faceNodes = this->FaceNodePool.data() + static_cast<std::size_t>(face.nodeOffset);
    if (face.nodeCount == 3)
    {
      for (int k = 0; k < 3; k++)
      {
        if ((faceNodes[k] != this->Cells[i].nodes[0]) && (faceNodes[k] != this->Cells[i].nodes[1]) &&
          (faceNodes[k] != this->Cells[i].nodes[2]) && (faceNodes[k] != this->Cells[i].nodes[3]))
        {
          this->Cells[i].nodes[4] = faceNodes[k];
        }
      }
    }
  }
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::PopulateWedgeCell(int i)
{
  this->Cells[i].nodes.resize(6);

  //  Find the first triangle face and make it the base.
  int base = 0;
  int first = 0;
  for (std::size_t j = 0; j < this->Cells[i].faces.size(); j++)
  {
    const int faceId = this->Cells[i].faces[j];
    if ((this->Faces[static_cast<std::size_t>(faceId)].type == 3) && (first == 0))
    {
      base = faceId;
      first = 1;
    }
  }

  //  Find the second triangle face and make it the top.
  int top = 0;
  int second = 0;
  for (std::size_t j = 0; j < this->Cells[i].faces.size(); j++)
  {
    const int faceId = this->Cells[i].faces[j];
    if ((this->Faces[static_cast<std::size_t>(faceId)].type == 3) && (second == 0) &&
      (faceId != base))
    {
      top = faceId;
      second = 1;
    }
  }

  // Load Base nodes into the nodes std::vector
  const auto& baseFace = this->Faces[static_cast<std::size_t>(base)];
  const auto& topFace = this->Faces[static_cast<std::size_t>(top)];
  const int* baseNodes =
    this->FaceNodePool.data() + static_cast<std::size_t>(baseFace.nodeOffset);
  const int* topNodes = this->FaceNodePool.data() + static_cast<std::size_t>(topFace.nodeOffset);
  if (baseFace.c0 == i)
  {
    for (int j = 0; j < 3; j++)
    {
      this->Cells[i].nodes[j] = baseNodes[j];
    }
  }
  else
  {
    for (int j = 2; j >= 0; j--)
    {
      this->Cells[i].nodes[2 - j] = baseNodes[j];
    }
  }
  // Load Top nodes into the nodes std::vector
  if (topFace.c1 == i)
  {
    for (int j = 3; j < 6; j++)
    {
      this->Cells[i].nodes[j] = topNodes[j - 3];
    }
  }
  else
  {
    for (int j = 3; j < 6; j++)
    {
      this->Cells[i].nodes[j] = topNodes[5 - j];
    }
  }

  //  Find the quad face with points 0 and 1 in them.
  int w01[4] = { -1, -1, -1, -1 };
  for (std::size_t j = 0; j < this->Cells[i].faces.size(); j++)
  {
    if (this->Cells[i].faces[j] != base && this->Cells[i].faces[j] != top)
    {
      int wf0 = 0;
      int wf1 = 0;
      const int faceId = this->Cells[i].faces[j];
      const auto& face = this->Faces[static_cast<std::size_t>(faceId)];
      const int* faceNodes = this->FaceNodePool.data() + static_cast<std::size_t>(face.nodeOffset);
      for (int k = 0; k < 4; k++)
      {
        if (this->Cells[i].nodes[0] == faceNodes[k])
        {
          wf0 = 1;
        }
        if (this->Cells[i].nodes[1] == faceNodes[k])
        {
          wf1 = 1;
        }
        if ((wf0 == 1) && (wf1 == 1))
        {
          for (int n = 0; n < 4; n++)
          {
            w01[n] = faceNodes[n];
          }
        }
      }
    }
  }

  //  Find the quad face with points 0 and 2 in them.
  int w02[4] = { -1, -1, -1, -1 };
  for (std::size_t j = 0; j < this->Cells[i].faces.size(); j++)
  {
    if (this->Cells[i].faces[j] != base && this->Cells[i].faces[j] != top)
    {
      int wf0 = 0;
      int wf2 = 0;
      const int faceId = this->Cells[i].faces[j];
      const auto& face = this->Faces[static_cast<std::size_t>(faceId)];
      const int* faceNodes = this->FaceNodePool.data() + static_cast<std::size_t>(face.nodeOffset);
      for (int k = 0; k < 4; k++)
      {
        if (this->Cells[i].nodes[0] == faceNodes[k])
        {
          wf0 = 1;
        }
        if (this->Cells[i].nodes[2] == faceNodes[k])
        {
          wf2 = 1;
        }
        if ((wf0 == 1) && (wf2 == 1))
        {
          for (int n = 0; n < 4; n++)
          {
            w02[n] = faceNodes[n];
          }
        }
      }
    }
  }

  // Point 3 is the point that is in both w01 and w02

  // What point is in f01 and f02 besides 0 ... this is point 3
  int p3 = 0;
  for (int k = 0; k < 4; k++)
  {
    if (w01[k] != this->Cells[i].nodes[0])
    {
      for (int n = 0; n < 4; n++)
      {
        if (w01[k] == w02[n])
        {
          p3 = w01[k];
        }
      }
    }
  }

  // Since we know point 3 now we check to see if points
  //  3, 4, and 5 are in the correct positions.
  int t[6];
  t[3] = this->Cells[i].nodes[3];
  t[4] = this->Cells[i].nodes[4];
  t[5] = this->Cells[i].nodes[5];
  if (p3 == this->Cells[i].nodes[4])
  {
    this->Cells[i].nodes[3] = t[4];
    this->Cells[i].nodes[4] = t[5];
    this->Cells[i].nodes[5] = t[3];
  }
  else if (p3 == this->Cells[i].nodes[5])
  {
    this->Cells[i].nodes[3] = t[5];
    this->Cells[i].nodes[4] = t[3];
    this->Cells[i].nodes[5] = t[4];
  }
  // else point 3 was lined up so everything was correct.
}

//------------------------------------------------------------------------------
void vtkFLUENTCFFReader::PopulatePolyhedronCell(int i)
{
  // Reconstruct polyhedron cell for VTK
  // For polyhedron cell, a special ptIds input format is used:
  // nodes stores the nodeIds while nodesOffset stores the node Offset for each faces
  auto& cell = this->Cells[i];
  cell.nodes.clear();
  cell.nodesOffset.clear();
  cell.nodePoolOffset = static_cast<vtkIdType>(this->CellNodePool.size());
  cell.nodeOffsetPoolOffset = static_cast<vtkIdType>(this->CellNodeOffsetPool.size());

  int currentOffset = 0;
  this->CellNodeOffsetPool.push_back(currentOffset);
  for (std::size_t j = 0; j < cell.faces.size(); j++)
  {
    const int faceId = cell.faces[j];
    const auto& face = this->Faces[static_cast<std::size_t>(faceId)];
    const int* faceNodes = this->FaceNodePool.data() + static_cast<std::size_t>(face.nodeOffset);
    std::size_t numFacePts = static_cast<std::size_t>(face.nodeCount);
    if (numFacePts != 0)
    {
      currentOffset += static_cast<int>(numFacePts);
      this->CellNodeOffsetPool.push_back(currentOffset);
      for (std::size_t k = 0; k < numFacePts; k++)
      {
        this->CellNodePool.push_back(faceNodes[k]);
      }
    }
  }
  cell.nodePoolCount = currentOffset;
  cell.nodeOffsetPoolCount = static_cast<int>(this->CellNodeOffsetPool.size() -
    static_cast<std::size_t>(cell.nodeOffsetPoolOffset));
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetData()
{
  this->CellDataChunks.clear();
  this->FaceDataChunks.clear();
  if (H5Gget_objinfo(this->HDFImpl->FluentDataFile, "/results/1", false, nullptr) == 0)
  {
    int iphase = 1;
    while (
      H5Gget_objinfo(this->HDFImpl->FluentDataFile,
        std::string("/results/1/phase-" + std::to_string(iphase)).c_str(), false, nullptr) == 0)
    {
      hid_t group;
      group = H5Gopen(this->HDFImpl->FluentDataFile,
        std::string("/results/1/phase-" + std::to_string(iphase)).c_str(), H5P_DEFAULT);
      if (group < 0)
      {
        vtkErrorMacro("Unable to open HDF group (GetData).");
        return 0;
      }
      if (!this->ReadDataForType(
            "cells", this->CellDataArraySelection, this->CellDataChunks,
            static_cast<vtkIdType>(this->Cells.size()), iphase))
      {
        CHECK_HDF(H5Gclose(group));
        return 0;
      }
      if (!this->ReadDataForType(
            "faces", this->FaceDataArraySelection, this->FaceDataChunks,
            static_cast<vtkIdType>(this->Faces.size()), iphase))
      {
        CHECK_HDF(H5Gclose(group));
        return 0;
      }
      CHECK_HDF(H5Gclose(group));
      iphase++;
    }
  }
  return 1;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::GetMetaData()
{
  if (H5Gget_objinfo(this->HDFImpl->FluentDataFile, "/results/1", false, nullptr) == 0)
  {
    int iphase = 1;
    while (
      H5Gget_objinfo(this->HDFImpl->FluentDataFile,
        std::string("/results/1/phase-" + std::to_string(iphase)).c_str(), false, nullptr) == 0)
    {
      hid_t group;
      group = H5Gopen(this->HDFImpl->FluentDataFile,
        std::string("/results/1/phase-" + std::to_string(iphase)).c_str(), H5P_DEFAULT);
      if (group < 0)
      {
        vtkErrorMacro("Unable to open HDF group (GetMetaData).");
        return 0;
      }
      if (!this->ReadMetadataForType("cells", this->CellDataArraySelection, this->PreReadCellData,
            iphase))
      {
        CHECK_HDF(H5Gclose(group));
        return 0;
      }
      if (!this->ReadMetadataForType("faces", this->FaceDataArraySelection, this->PreReadFaceData,
            iphase))
      {
        CHECK_HDF(H5Gclose(group));
        return 0;
      }
      CHECK_HDF(H5Gclose(group));
      iphase++;
    }
  }
  return 1;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::ReadMetadataForType(const std::string& groupName,
  vtkDataArraySelection* vtkNotUsed(selection), std::vector<std::string>& preReadData,
  int phaseIndex)
{
  const std::string baseGroupName =
    std::string("/results/1/phase-") + std::to_string(phaseIndex) + "/" + groupName;
  if (H5Gget_objinfo(this->HDFImpl->FluentDataFile, baseGroupName.c_str(), false, nullptr) != 0)
  {
    return 1;
  }

  hid_t group = H5Gopen(this->HDFImpl->FluentDataFile, baseGroupName.c_str(), H5P_DEFAULT);
  if (group < 0)
  {
    vtkErrorMacro("Unable to open HDF group (ReadMetadataForType).");
    return 0;
  }

  hid_t dset = H5Dopen(group, "fields", H5P_DEFAULT);
  if (dset < 0)
  {
    CHECK_HDF(H5Gclose(group));
    throw std::runtime_error("Unable to open HDF dataset (ReadMetadataForType).");
  }

  hid_t space = H5Dget_space(dset);
  hid_t dataType = H5Dget_type(dset);
  std::size_t stringLength = H5Tget_size(dataType);
  char* strchar = new char[stringLength];
  CHECK_HDF(H5Dread(dset, dataType, H5S_ALL, H5S_ALL, H5P_DEFAULT, strchar));
  CHECK_HDF(H5Dclose(dset));
  CHECK_HDF(H5Tclose(dataType));
  CHECK_HDF(H5Sclose(space));
  std::string str(strchar);
  delete[] strchar;

  std::vector<std::string> fieldNames = SplitFieldNames(str);
  preReadData.reserve(preReadData.size() + fieldNames.size());
  for (auto fieldName : fieldNames)
  {
    hid_t groupdata = H5Gopen(group, fieldName.c_str(), H5P_DEFAULT);
    if (groupdata < 0)
    {
      CHECK_HDF(H5Gclose(group));
      vtkErrorMacro("Unable to open HDF group (ReadMetadataForType field).");
      return 0;
    }

    if (this->RenameArrays)
    {
      fieldName = vtkFLUENTCFFInternal::GetMatchingFieldName(fieldName);
    }
    if (phaseIndex > 1)
    {
      fieldName += std::string("-phase_") + std::to_string(phaseIndex - 1);
    }

    uint64_t nSections = 0;
    hid_t attr = H5Aopen(groupdata, "nSections", H5P_DEFAULT);
    if (attr < 0)
    {
      CHECK_HDF(H5Gclose(groupdata));
      CHECK_HDF(H5Gclose(group));
      throw std::runtime_error("Unable to open HDF attribute (ReadMetadataForType).");
    }
    CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &nSections));
    CHECK_HDF(H5Aclose(attr));

    if (nSections > 0)
    {
      preReadData.push_back(fieldName);
    }
    CHECK_HDF(H5Gclose(groupdata));
  }

  CHECK_HDF(H5Gclose(group));
  return 1;
}

//------------------------------------------------------------------------------
int vtkFLUENTCFFReader::ReadDataForType(const std::string& groupName,
  vtkDataArraySelection* selection, std::vector<DataChunk>& chunks, vtkIdType tupleCount,
  int phaseIndex)
{
  const std::string baseGroupName =
    std::string("/results/1/phase-") + std::to_string(phaseIndex) + "/" + groupName;
  if (H5Gget_objinfo(this->HDFImpl->FluentDataFile, baseGroupName.c_str(), false, nullptr) != 0)
  {
    return 1;
  }

  hid_t group = H5Gopen(this->HDFImpl->FluentDataFile, baseGroupName.c_str(), H5P_DEFAULT);
  if (group < 0)
  {
    vtkErrorMacro("Unable to open HDF group (ReadDataForType).");
    return 0;
  }

  hid_t dset = H5Dopen(group, "fields", H5P_DEFAULT);
  if (dset < 0)
  {
    CHECK_HDF(H5Gclose(group));
    throw std::runtime_error("Unable to open HDF dataset (ReadDataForType).");
  }

  hid_t space = H5Dget_space(dset);
  hid_t dataType = H5Dget_type(dset);
  std::size_t stringLength = H5Tget_size(dataType);
  char* strchar = new char[stringLength];
  CHECK_HDF(H5Dread(dset, dataType, H5S_ALL, H5S_ALL, H5P_DEFAULT, strchar));
  CHECK_HDF(H5Dclose(dset));
  CHECK_HDF(H5Tclose(dataType));
  CHECK_HDF(H5Sclose(space));
  std::string str(strchar);
  delete[] strchar;

  std::vector<std::string> fieldNames = SplitFieldNames(str);
  chunks.reserve(chunks.size() + fieldNames.size());
  for (auto originalFieldName : fieldNames)
  {
    hid_t groupdata = H5Gopen(group, originalFieldName.c_str(), H5P_DEFAULT);
    if (groupdata < 0)
    {
      CHECK_HDF(H5Gclose(group));
      vtkErrorMacro("Unable to open HDF group (ReadDataForType field).");
      return 0;
    }

    std::string selectedName = originalFieldName;
    if (this->RenameArrays)
    {
      selectedName = vtkFLUENTCFFInternal::GetMatchingFieldName(selectedName);
    }
    if (phaseIndex > 1)
    {
      selectedName += std::string("-phase_") + std::to_string(phaseIndex - 1);
    }

    if (selection->ArrayIsEnabled(selectedName.c_str()))
    {
      uint64_t nSections = 0;
      hid_t attr = H5Aopen(groupdata, "nSections", H5P_DEFAULT);
      if (attr < 0)
      {
        CHECK_HDF(H5Gclose(groupdata));
        CHECK_HDF(H5Gclose(group));
        throw std::runtime_error("Unable to open HDF attribute (ReadDataForType).");
      }
      CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &nSections));
      CHECK_HDF(H5Aclose(attr));

      DataChunk chunk;
      chunk.variableName = selectedName;
      chunk.dim = 0;

      std::vector<double> sectionData;
      std::vector<float> sectionDataf;
      for (uint64_t iSection = 0; iSection < nSections; iSection++)
      {
        dset = H5Dopen(groupdata, std::to_string(iSection + 1).c_str(), H5P_DEFAULT);
        if (dset < 0)
        {
          CHECK_HDF(H5Gclose(groupdata));
          CHECK_HDF(H5Gclose(group));
          throw std::runtime_error("Unable to open HDF dataset (ReadDataForType section).");
        }

        uint64_t minId = 0;
        uint64_t maxId = 0;
        attr = H5Aopen(dset, "minId", H5P_DEFAULT);
        if (attr < 0)
        {
          CHECK_HDF(H5Dclose(dset));
          CHECK_HDF(H5Gclose(groupdata));
          CHECK_HDF(H5Gclose(group));
          throw std::runtime_error("Unable to open HDF attribute (ReadDataForType minId).");
        }
        CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &minId));
        CHECK_HDF(H5Aclose(attr));
        attr = H5Aopen(dset, "maxId", H5P_DEFAULT);
        if (attr < 0)
        {
          CHECK_HDF(H5Dclose(dset));
          CHECK_HDF(H5Gclose(groupdata));
          CHECK_HDF(H5Gclose(group));
          throw std::runtime_error("Unable to open HDF attribute (ReadDataForType maxId).");
        }
        CHECK_HDF(H5Aread(attr, H5T_NATIVE_UINT64, &maxId));
        CHECK_HDF(H5Aclose(attr));

        space = H5Dget_space(dset);
        hid_t ndims = H5Sget_simple_extent_ndims(space);
        std::vector<hsize_t> dims(ndims);
        CHECK_HDF(H5Sget_simple_extent_dims(space, dims.data(), nullptr));
        hsize_t totalDim = 1;
        for (hid_t k = 0; k < ndims; k++)
        {
          totalDim *= dims[k];
        }

        int typePrec = 0;
        hid_t type = H5Dget_type(dset);
        if (H5Tget_precision(type) == 32)
        {
          typePrec = 1;
        }
        CHECK_HDF(H5Tclose(type));

        sectionData.resize(static_cast<std::size_t>(totalDim));
        if (typePrec == 0)
        {
          CHECK_HDF(H5Dread(
            dset, H5T_NATIVE_DOUBLE, H5S_ALL, H5S_ALL, H5P_DEFAULT, sectionData.data()));
        }
        else
        {
          sectionDataf.resize(static_cast<std::size_t>(totalDim));
          CHECK_HDF(
            H5Dread(dset, H5T_NATIVE_FLOAT, H5S_ALL, H5S_ALL, H5P_DEFAULT, sectionDataf.data()));
          for (std::size_t j = 0; j < totalDim; j++)
          {
            sectionData[j] = static_cast<double>(sectionDataf[j]);
          }
        }

        if (ndims > 3)
        {
          vtkWarningMacro("The field " << selectedName << " has more than 3 dimensions");
          CHECK_HDF(H5Sclose(space));
          CHECK_HDF(H5Dclose(dset));
          continue;
        }

        int numberOfComponents = 1;
        if (ndims > 1)
        {
          numberOfComponents = static_cast<int>(dims[1]);
        }
        if (numberOfComponents > 9)
        {
          vtkWarningMacro(
            "The field " << selectedName << " has more than 9 components, it can't be parsed.");
          CHECK_HDF(H5Sclose(space));
          CHECK_HDF(H5Dclose(dset));
          continue;
        }

        if (chunk.dim == 0)
        {
          chunk.dim = static_cast<size_t>(numberOfComponents);
          chunk.dataVector.assign(static_cast<std::size_t>(tupleCount) * chunk.dim,
            std::numeric_limits<double>::quiet_NaN());
        }

        for (uint64_t j = minId; j <= maxId; j++)
        {
          const std::size_t tupleIndex = static_cast<std::size_t>(j - 1);
          if (tupleIndex >= static_cast<std::size_t>(tupleCount))
          {
            continue;
          }
          for (int k = 0; k < numberOfComponents; k++)
          {
            chunk.dataVector[tupleIndex * chunk.dim + static_cast<std::size_t>(k)] =
              sectionData[k * (maxId - minId + 1) + (j - minId)];
          }
        }

        if (groupName == "faces")
        {
          chunk.FaceSectionFluentIdRanges1Based.emplace_back(minId, maxId);
        }

        CHECK_HDF(H5Sclose(space));
        CHECK_HDF(H5Dclose(dset));
      }

      if (chunk.dim > 0)
      {
        this->NumberOfArrays++;
        chunks.emplace_back(std::move(chunk));
      }
    }

    CHECK_HDF(H5Gclose(groupdata));
  }

  CHECK_HDF(H5Gclose(group));
  return 1;
}
VTK_ABI_NAMESPACE_END
