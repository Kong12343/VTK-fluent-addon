#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <memory>
#include <string>
#include <vector>

#include <torch/extension.h>

#include "FluentCFFGNNExporter.h"
#include "vtkFLUENTCFFReader.h"

namespace py = pybind11;

static py::dict GraphTensorsToDict(const FluentCFFGNNExporter::GraphTensors& g)
{
  py::dict d;
  d["boundary_coords"] = g.boundary_coords;
  d["boundary_normals"] = g.boundary_normals;
  d["boundary_labels"] = g.boundary_labels;
  d["zoneType_values"] = g.zoneType_values;
  d["internal_coords"] = g.internal_coords;
  d["edge_index"] = g.edge_index;
  d["face_areas"] = g.face_areas;
  d["cell_face_areas"] = g.cell_face_areas;
  return d;
}

static py::dict FieldTensorToDict(const FluentCFFGNNExporter::FieldTensor& f)
{
  py::dict d;
  d["values"] = f.values;
  d["names"] = f.names;
  return d;
}

PYBIND11_MODULE(fluentcff_gnn, m)
{
  m.doc() = "FluentCFF -> GNN tensor exporter (VTK + LibTorch) with pybind11 bindings";

  // Non-owning: lifetime is owned by FluentCFFGNNExporter (vtkSmartPointer). Do not call New() from Python.
  py::class_<vtkFLUENTCFFReader, std::unique_ptr<vtkFLUENTCFFReader, py::nodelete>>(m, "vtkFLUENTCFFReader")
    // Basic file controls
    .def("SetFileName", &vtkFLUENTCFFReader::SetFileName)
    .def("GetFileName", &vtkFLUENTCFFReader::GetFileName)
    .def("SetDataFileName", &vtkFLUENTCFFReader::SetDataFileName)
    .def("GetDataFileName", &vtkFLUENTCFFReader::GetDataFileName)
    .def("SetRenameArrays", &vtkFLUENTCFFReader::SetRenameArrays)
    .def("GetRenameArrays", &vtkFLUENTCFFReader::GetRenameArrays)
    .def(
      "SetExcludedFieldArrayNames",
      [](vtkFLUENTCFFReader& r, const std::vector<std::string>& names) {
        r.SetExcludedFieldArrayNames(names);
      })
    .def("ClearExcludedFieldArrayNames", &vtkFLUENTCFFReader::ClearExcludedFieldArrayNames)
    .def(
      "GetExcludedFieldArrayNames",
      [](const vtkFLUENTCFFReader& r) { return r.GetExcludedFieldArrayNames(); })
    // vtkAlgorithm provides multiple Update overloads; MSVC needs an explicit lambda.
    .def("Update", [](vtkFLUENTCFFReader& r) { r.Update(); })
    // Zones
    .def("GetNumberOfFaceZones", &vtkFLUENTCFFReader::GetNumberOfFaceZones)
    .def("GetFaceZoneName", &vtkFLUENTCFFReader::GetFaceZoneName)
    .def("GetFaceZoneIdByName", &vtkFLUENTCFFReader::GetFaceZoneIdByName)
    .def("GetFaceZoneType", &vtkFLUENTCFFReader::GetFaceZoneType)
    .def("GetCellZoneType", &vtkFLUENTCFFReader::GetCellZoneType)
    // Graph/boundary
    .def("GetCellCentroidCount", &vtkFLUENTCFFReader::GetCellCentroidCount)
    .def("GetCellCentroids", [](const vtkFLUENTCFFReader& r) {
      const int n = r.GetCellCentroidCount();
      const float* p = r.GetCellCentroids();
      return torch::from_blob(const_cast<float*>(p), { n, 3 }, torch::TensorOptions().dtype(torch::kFloat32)).clone();
    })
    .def("GetEdgeIndex", [](const vtkFLUENTCFFReader& r) {
      std::vector<int> src, dst;
      r.GetEdgeIndex(src, dst);
      const int64_t e = static_cast<int64_t>(std::min(src.size(), dst.size()));
      auto t = torch::empty({ 2, e }, torch::TensorOptions().dtype(torch::kInt64));
      auto* ptr = t.data_ptr<int64_t>();
      for (int64_t i = 0; i < e; ++i)
      {
        ptr[i] = src[static_cast<std::size_t>(i)];
        ptr[e + i] = dst[static_cast<std::size_t>(i)];
      }
      return t;
    })
    .def("GetBoundaryFaceCount", &vtkFLUENTCFFReader::GetBoundaryFaceCount)
    .def("GetFaceCentroids", [](const vtkFLUENTCFFReader& r) {
      const int n = r.GetBoundaryFaceCount();
      const float* p = r.GetFaceCentroids();
      return torch::from_blob(const_cast<float*>(p), { n, 3 }, torch::TensorOptions().dtype(torch::kFloat32)).clone();
    })
    .def("GetFaceNormals", [](const vtkFLUENTCFFReader& r) {
      const int n = r.GetBoundaryFaceCount();
      const float* p = r.GetFaceNormals();
      return torch::from_blob(const_cast<float*>(p), { n, 3 }, torch::TensorOptions().dtype(torch::kFloat32)).clone();
    })
    .def("GetFaceAreas", [](const vtkFLUENTCFFReader& r) {
      const int n = r.GetFaceAreaCount();
      const float* p = r.GetFaceAreas();
      if (n <= 0 || !p) return torch::empty({ 0 }, torch::kFloat32);
      return torch::from_blob(const_cast<float*>(p), { n }, torch::TensorOptions().dtype(torch::kFloat32)).clone();
    })
    .def("GetCellFaceAreas", [](const vtkFLUENTCFFReader& r) {
      const int n = r.GetCellFaceAreaCount();
      const float* p = r.GetCellFaceAreas();
      if (n <= 0 || !p) return torch::empty({ 0 }, torch::kFloat32);
      return torch::from_blob(const_cast<float*>(p), { n }, torch::TensorOptions().dtype(torch::kFloat32)).clone();
    })
    // Loaded chunk introspection
    .def("GetLoadedCellChunkCount", &vtkFLUENTCFFReader::GetLoadedCellChunkCount)
    .def("GetLoadedCellChunkName", &vtkFLUENTCFFReader::GetLoadedCellChunkName)
    .def("GetLoadedCellChunkDim", &vtkFLUENTCFFReader::GetLoadedCellChunkDim)
    .def("GetLoadedFaceChunkCount", &vtkFLUENTCFFReader::GetLoadedFaceChunkCount)
    .def("GetLoadedFaceChunkName", &vtkFLUENTCFFReader::GetLoadedFaceChunkName)
    .def("GetLoadedFaceChunkDim", &vtkFLUENTCFFReader::GetLoadedFaceChunkDim);

  py::class_<FluentCFFGNNExporter>(m, "Exporter")
    .def(py::init<>())
    .def("GetReader", [](FluentCFFGNNExporter& e) {
      return std::unique_ptr<vtkFLUENTCFFReader, py::nodelete>(e.GetReader());
    })
    .def("SetCaseFileName", &FluentCFFGNNExporter::SetCaseFileName)
    .def("SetDataFileName", &FluentCFFGNNExporter::SetDataFileName)
    .def("SetRenameArrays", &FluentCFFGNNExporter::SetRenameArrays)
    .def("EnableAllCellArrays", &FluentCFFGNNExporter::EnableAllCellArrays)
    .def("EnableAllFaceArrays", &FluentCFFGNNExporter::EnableAllFaceArrays)
    .def(
      "SetExcludedFieldArrayNames",
      [](FluentCFFGNNExporter& e, std::vector<std::string> names) {
        e.SetExcludedFieldArrayNames(std::move(names));
      })
    .def("ClearExcludedFieldArrayNames", &FluentCFFGNNExporter::ClearExcludedFieldArrayNames)
    .def("SetMaxExportedFieldColumns", &FluentCFFGNNExporter::SetMaxExportedFieldColumns)
    .def("GetMaxExportedFieldColumns", &FluentCFFGNNExporter::GetMaxExportedFieldColumns)
    .def("SetSparsifyEpsilon", &FluentCFFGNNExporter::SetSparsifyEpsilon)
    .def("GetSparsifyEpsilon", &FluentCFFGNNExporter::GetSparsifyEpsilon)
    .def("SetSparsifySeed", &FluentCFFGNNExporter::SetSparsifySeed)
    .def("GetSparsifySeed", &FluentCFFGNNExporter::GetSparsifySeed)
    .def("Update", &FluentCFFGNNExporter::Update)
    .def("ExtractGraphTensors", [](const FluentCFFGNNExporter& e) { return GraphTensorsToDict(e.ExtractGraphTensors()); })
    .def("ExtractCellFieldTensor", [](const FluentCFFGNNExporter& e) { return FieldTensorToDict(e.ExtractCellFieldTensor()); })
    .def("ExtractBoundaryFieldTensor", [](const FluentCFFGNNExporter& e) { return FieldTensorToDict(e.ExtractBoundaryFieldTensor()); });
}

