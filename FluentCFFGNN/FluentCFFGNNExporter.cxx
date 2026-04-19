#include "FluentCFFGNNExporter.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include <string>

#include "vtkFLUENTCFFReader.h"

FluentCFFGNNExporter::FluentCFFGNNExporter()
  : Reader(vtkSmartPointer<vtkFLUENTCFFReader>::New())
{
}

FluentCFFGNNExporter::~FluentCFFGNNExporter() = default;

vtkFLUENTCFFReader* FluentCFFGNNExporter::GetReader() const
{
  return this->Reader.Get();
}

void FluentCFFGNNExporter::SetCaseFileName(std::string caseFile)
{
  this->CaseFileName = std::move(caseFile);
}

void FluentCFFGNNExporter::SetDataFileName(std::string dataFile)
{
  this->DataFileName = std::move(dataFile);
}

void FluentCFFGNNExporter::SetRenameArrays(bool renameArrays)
{
  this->RenameArrays = renameArrays;
}

void FluentCFFGNNExporter::EnableAllCellArrays()
{
  this->Reader->EnableAllCellArrays();
}

void FluentCFFGNNExporter::EnableAllFaceArrays()
{
  this->Reader->EnableAllFaceArrays();
}

void FluentCFFGNNExporter::SetExcludedFieldArrayNames(std::vector<std::string> names)
{
  this->ExcludedFieldArrayNames = std::move(names);
}

void FluentCFFGNNExporter::ClearExcludedFieldArrayNames()
{
  this->ExcludedFieldArrayNames.clear();
}

void FluentCFFGNNExporter::SetMaxExportedFieldColumns(int maxK)
{
  this->MaxExportedFieldColumns = maxK;
}

int FluentCFFGNNExporter::GetMaxExportedFieldColumns() const
{
  return this->MaxExportedFieldColumns;
}

void FluentCFFGNNExporter::Update()
{
  if (this->CaseFileName.empty())
  {
    throw std::runtime_error("CaseFileName is empty.");
  }
  this->Reader->SetFileName(this->CaseFileName);
  this->Reader->SetRenameArrays(this->RenameArrays);
  if (!this->DataFileName.empty())
  {
    this->Reader->SetDataFileName(this->DataFileName);
  }
  this->Reader->SetExcludedFieldArrayNames(this->ExcludedFieldArrayNames);
  this->Reader->Update();
}

void FluentCFFGNNExporter::ThrowIfExpandedExceedsMax(const std::vector<std::string>& expandedNames) const
{
  if (this->MaxExportedFieldColumns <= 0)
  {
    return;
  }
  const int k = static_cast<int>(expandedNames.size());
  if (k > this->MaxExportedFieldColumns)
  {
    throw std::runtime_error(
      "FluentCFFGNNExporter: expanded field column count K=" + std::to_string(k) +
      " exceeds MaxExportedFieldColumns=" + std::to_string(this->MaxExportedFieldColumns));
  }
}

std::vector<std::string> FluentCFFGNNExporter::SortedIntersectedChunkNames() const
{
  const int nCell = this->Reader->GetLoadedCellChunkCount();
  const int nFace = this->Reader->GetLoadedFaceChunkCount();
  std::unordered_set<std::string> faceNames;
  faceNames.reserve(static_cast<std::size_t>(nFace) * 2 + 1);
  for (int i = 0; i < nFace; ++i)
  {
    const char* n = this->Reader->GetLoadedFaceChunkName(i);
    if (n)
    {
      faceNames.emplace(n);
    }
  }

  std::vector<std::string> intersection;
  for (int i = 0; i < nCell; ++i)
  {
    const char* n = this->Reader->GetLoadedCellChunkName(i);
    if (!n)
    {
      continue;
    }
    std::string name(n);
    if (faceNames.count(name) > 0)
    {
      intersection.push_back(std::move(name));
    }
  }

  std::sort(intersection.begin(), intersection.end());
  intersection.erase(std::unique(intersection.begin(), intersection.end()), intersection.end());
  return intersection;
}

torch::Tensor FluentCFFGNNExporter::MakeFloatTensorFromFlat(
  const float* data, std::int64_t n, std::int64_t d) const
{
  if (n <= 0 || d <= 0)
  {
    return torch::empty({ n, d }, torch::TensorOptions().dtype(torch::kFloat32));
  }
  auto t = torch::empty({ n, d }, torch::TensorOptions().dtype(torch::kFloat32));
  if (data)
  {
    std::memcpy(t.data_ptr(), data, static_cast<std::size_t>(n * d) * sizeof(float));
  }
  else
  {
    t.zero_();
  }
  return t;
}

torch::Tensor FluentCFFGNNExporter::MakeFloatTensorFromFlat(
  const std::vector<float>& data, std::int64_t d) const
{
  const std::int64_t n = d > 0 ? static_cast<std::int64_t>(data.size()) / d : 0;
  return this->MakeFloatTensorFromFlat(data.empty() ? nullptr : data.data(), n, d);
}

std::vector<std::string> FluentCFFGNNExporter::ExpandComponentNames(
  const std::string& base, int dim)
{
  std::vector<std::string> names;
  if (dim <= 1)
  {
    names.push_back(base);
    return names;
  }
  names.reserve(static_cast<std::size_t>(dim));
  for (int c = 0; c < dim; ++c)
  {
    names.push_back(base + "[" + std::to_string(c) + "]");
  }
  return names;
}

FluentCFFGNNExporter::GraphTensors FluentCFFGNNExporter::ExtractGraphTensors() const
{
  return this->ExtractGraphTensorsImpl();
}

FluentCFFGNNExporter::FieldTensor FluentCFFGNNExporter::ExtractCellFieldTensor() const
{
  return this->ExtractCellFieldTensorImpl();
}

FluentCFFGNNExporter::FieldTensor FluentCFFGNNExporter::ExtractBoundaryFieldTensor() const
{
  return this->ExtractBoundaryFieldTensorImpl();
}

FluentCFFGNNExporter::GraphTensors FluentCFFGNNExporter::ExtractGraphTensorsImpl() const
{
  GraphTensors out;

  const int nc = this->Reader->GetCellCentroidCount();
  const float* cellCentroids = this->Reader->GetCellCentroids();
  out.internal_coords = this->MakeFloatTensorFromFlat(cellCentroids, nc, 3);

  // edge_index
  std::vector<int> src, dst;
  this->Reader->GetEdgeIndex(src, dst);
  const std::int64_t e = static_cast<std::int64_t>(std::min(src.size(), dst.size()));
  auto edge = torch::empty({ 2, e }, torch::TensorOptions().dtype(torch::kInt64));
  auto* edgePtr = edge.data_ptr<std::int64_t>();
  for (std::int64_t i = 0; i < e; ++i)
  {
    edgePtr[i] = static_cast<std::int64_t>(src[static_cast<std::size_t>(i)]);
    edgePtr[e + i] = static_cast<std::int64_t>(dst[static_cast<std::size_t>(i)]);
  }
  out.edge_index = edge;

  // boundary coords/normals
  const int nb = this->Reader->GetBoundaryFaceCount();
  const float* faceCentroids = this->Reader->GetFaceCentroids();
  const float* faceNormals = this->Reader->GetFaceNormals();
  out.boundary_coords = this->MakeFloatTensorFromFlat(faceCentroids, nb, 3);
  out.boundary_normals = this->MakeFloatTensorFromFlat(faceNormals, nb, 3);

  // zoneType compact mapping: iterate face zones in reader order.
  struct ZoneSpan
  {
    int zoneType = -1;
    vtkIdType count = 0;
  };
  std::vector<ZoneSpan> spans;
  spans.reserve(static_cast<std::size_t>(this->Reader->GetNumberOfFaceZones()));
  std::vector<int> zoneTypesPresent;
  for (int zi = 0; zi < this->Reader->GetNumberOfFaceZones(); ++zi)
  {
    const char* name = this->Reader->GetFaceZoneName(zi);
    if (!name)
    {
      continue;
    }
    const int zoneId = this->Reader->GetFaceZoneIdByName(name);
    const int zoneType = this->Reader->GetFaceZoneType(zoneId);
    if (zoneType == 2) // interior
    {
      continue;
    }
    const vtkIdType first = this->Reader->GetFaceZoneFirstFaceIdByName(name);
    const vtkIdType last = this->Reader->GetFaceZoneLastFaceIdByName(name);
    if (first < 0 || last < first)
    {
      continue;
    }
    const vtkIdType count = last - first + 1;
    spans.push_back({ zoneType, count });
    zoneTypesPresent.push_back(zoneType);
  }
  std::sort(zoneTypesPresent.begin(), zoneTypesPresent.end());
  zoneTypesPresent.erase(std::unique(zoneTypesPresent.begin(), zoneTypesPresent.end()),
    zoneTypesPresent.end());
  out.zoneType_values = zoneTypesPresent;

  const std::int64_t C = static_cast<std::int64_t>(zoneTypesPresent.size());
  out.boundary_labels = torch::zeros({ nb, C }, torch::TensorOptions().dtype(torch::kFloat32));
  if (nb > 0 && C > 0)
  {
    std::unordered_map<int, std::int64_t> typeToCol;
    typeToCol.reserve(zoneTypesPresent.size());
    for (std::size_t i = 0; i < zoneTypesPresent.size(); ++i)
    {
      typeToCol[zoneTypesPresent[i]] = static_cast<std::int64_t>(i);
    }

    auto* lbl = out.boundary_labels.data_ptr<float>();
    std::int64_t row = 0;
    for (const auto& sp : spans)
    {
      auto it = typeToCol.find(sp.zoneType);
      if (it == typeToCol.end())
      {
        row += static_cast<std::int64_t>(sp.count);
        continue;
      }
      const std::int64_t col = it->second;
      for (vtkIdType k = 0; k < sp.count && row < nb; ++k, ++row)
      {
        lbl[row * C + col] = 1.0f;
      }
    }
  }

  return out;
}

FluentCFFGNNExporter::FieldTensor FluentCFFGNNExporter::ExtractCellFieldTensorImpl() const
{
  FieldTensor out;
  const int nc = this->Reader->GetCellCentroidCount();
  const std::vector<std::string> sorted = this->SortedIntersectedChunkNames();
  if (sorted.empty() || nc <= 0)
  {
    out.values = torch::empty({ nc, 0 }, torch::TensorOptions().dtype(torch::kFloat32));
    return out;
  }

  std::int64_t K = 0;
  for (const auto& baseName : sorted)
  {
    int dim = 0;
    for (int i = 0; i < this->Reader->GetLoadedCellChunkCount(); ++i)
    {
      const char* n = this->Reader->GetLoadedCellChunkName(i);
      if (n && baseName == n)
      {
        dim = this->Reader->GetLoadedCellChunkDim(i);
        break;
      }
    }
    if (dim <= 0)
    {
      throw std::runtime_error(
        "ExtractCellFieldTensor: missing cell chunk for array \"" + baseName + "\"");
    }
    K += static_cast<std::int64_t>(dim);
    auto expanded = ExpandComponentNames(baseName, dim);
    out.names.insert(out.names.end(), expanded.begin(), expanded.end());
  }

  this->ThrowIfExpandedExceedsMax(out.names);

  out.values = torch::empty({ nc, K }, torch::TensorOptions().dtype(torch::kFloat32));
  auto* dst = out.values.data_ptr<float>();
  std::int64_t col0 = 0;
  for (const auto& baseName : sorted)
  {
    const double* src = nullptr;
    int dim = 0;
    vtkIdType tuples = 0;
    for (int i = 0; i < this->Reader->GetLoadedCellChunkCount(); ++i)
    {
      const char* n = this->Reader->GetLoadedCellChunkName(i);
      if (n && baseName == n)
      {
        dim = this->Reader->GetLoadedCellChunkDim(i);
        src = this->Reader->GetLoadedCellChunkData(i);
        tuples = this->Reader->GetLoadedCellChunkTupleCount(i);
        break;
      }
    }
    if (!src || dim <= 0 || tuples < nc)
    {
      throw std::runtime_error(
        "ExtractCellFieldTensor: invalid cell chunk for array \"" + baseName + "\"");
    }
    for (int r = 0; r < nc; ++r)
    {
      const std::size_t base = static_cast<std::size_t>(r) * static_cast<std::size_t>(dim);
      for (int c = 0; c < dim; ++c)
      {
        const double v = src[base + static_cast<std::size_t>(c)];
        dst[static_cast<std::size_t>(r) * static_cast<std::size_t>(K) +
          static_cast<std::size_t>(col0 + c)] =
          std::isfinite(v) ? static_cast<float>(v) : 0.0f;
      }
    }
    col0 += static_cast<std::int64_t>(dim);
  }

  return out;
}

FluentCFFGNNExporter::FieldTensor FluentCFFGNNExporter::ExtractBoundaryFieldTensorImpl() const
{
  FieldTensor out;
  const int nb = this->Reader->GetBoundaryFaceCount();
  const int faceChunks = this->Reader->GetLoadedFaceChunkCount();
  const int cellChunks = this->Reader->GetLoadedCellChunkCount();
  if (nb <= 0 || faceChunks <= 0 || cellChunks <= 0)
  {
    out.values = torch::empty({ nb, 0 }, torch::TensorOptions().dtype(torch::kFloat32));
    return out;
  }

  const std::vector<std::string> sorted = this->SortedIntersectedChunkNames();
  if (sorted.empty())
  {
    out.values = torch::empty({ nb, 0 }, torch::TensorOptions().dtype(torch::kFloat32));
    return out;
  }

  std::int64_t K = 0;
  for (const auto& baseName : sorted)
  {
    int cellDim = 0;
    for (int i = 0; i < this->Reader->GetLoadedCellChunkCount(); ++i)
    {
      const char* n = this->Reader->GetLoadedCellChunkName(i);
      if (n && baseName == n)
      {
        cellDim = this->Reader->GetLoadedCellChunkDim(i);
        break;
      }
    }
    int faceDim = 0;
    for (int i = 0; i < faceChunks; ++i)
    {
      const char* n = this->Reader->GetLoadedFaceChunkName(i);
      if (n && baseName == n)
      {
        faceDim = this->Reader->GetLoadedFaceChunkDim(i);
        break;
      }
    }
    if (cellDim <= 0 || faceDim <= 0 || cellDim != faceDim)
    {
      throw std::runtime_error(
        "ExtractBoundaryFieldTensor: cell/face chunk dimension mismatch for \"" + baseName + "\"");
    }
    K += static_cast<std::int64_t>(cellDim);
    auto expanded = ExpandComponentNames(baseName, cellDim);
    out.names.insert(out.names.end(), expanded.begin(), expanded.end());
  }

  this->ThrowIfExpandedExceedsMax(out.names);

  out.values = torch::empty({ nb, K }, torch::TensorOptions().dtype(torch::kFloat32));
  out.values.zero_();
  if (K == 0)
  {
    return out;
  }

  std::vector<vtkIdType> boundaryFaceIds;
  boundaryFaceIds.reserve(static_cast<std::size_t>(nb));
  for (int zi = 0; zi < this->Reader->GetNumberOfFaceZones(); ++zi)
  {
    const char* zoneName = this->Reader->GetFaceZoneName(zi);
    if (!zoneName)
    {
      continue;
    }
    const int zoneId = this->Reader->GetFaceZoneIdByName(zoneName);
    const int zoneType = this->Reader->GetFaceZoneType(zoneId);
    if (zoneType == 2)
    {
      continue;
    }
    const vtkIdType first = this->Reader->GetFaceZoneFirstFaceIdByName(zoneName);
    const vtkIdType last = this->Reader->GetFaceZoneLastFaceIdByName(zoneName);
    if (first < 0 || last < first)
    {
      continue;
    }
    for (vtkIdType faceId = first; faceId <= last; ++faceId)
    {
      boundaryFaceIds.push_back(faceId);
      if (static_cast<int>(boundaryFaceIds.size()) >= nb)
      {
        break;
      }
    }
    if (static_cast<int>(boundaryFaceIds.size()) >= nb)
    {
      break;
    }
  }

  auto* dst = out.values.data_ptr<float>();
  std::int64_t col0 = 0;
  for (const auto& baseName : sorted)
  {
    const double* data = nullptr;
    int dim = 0;
    for (int i = 0; i < faceChunks; ++i)
    {
      const char* n = this->Reader->GetLoadedFaceChunkName(i);
      if (n && baseName == n)
      {
        dim = this->Reader->GetLoadedFaceChunkDim(i);
        data = this->Reader->GetLoadedFaceChunkData(i);
        break;
      }
    }
    if (!data || dim <= 0)
    {
      throw std::runtime_error(
        "ExtractBoundaryFieldTensor: missing face chunk for array \"" + baseName + "\"");
    }
    for (int r = 0; r < nb && r < static_cast<int>(boundaryFaceIds.size()); ++r)
    {
      const vtkIdType faceId = boundaryFaceIds[static_cast<std::size_t>(r)];
      const std::size_t base =
        static_cast<std::size_t>(faceId) * static_cast<std::size_t>(dim);
      for (int c = 0; c < dim; ++c)
      {
        const double v = data[base + static_cast<std::size_t>(c)];
        dst[static_cast<std::size_t>(r) * static_cast<std::size_t>(K) +
          static_cast<std::size_t>(col0 + c)] =
          std::isfinite(v) ? static_cast<float>(v) : 0.0f;
      }
    }
    col0 += static_cast<std::int64_t>(dim);
  }

  return out;
}
