#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <torch/torch.h>
#include <vtkSmartPointer.h>

class vtkFLUENTCFFReader;

/**
 * Fluent CFF -> GNN tensor exporter.
 *
 * Boundary points are boundary faces (zoneType != interior) represented by face centroids.
 * zoneType one-hot uses a compact mapping over zoneTypes present in the current case.
 */
class FluentCFFGNNExporter
{
public:
  struct FieldTensor
  {
    torch::Tensor values;            // [N, K]
    std::vector<std::string> names;  // length K
  };

  struct GraphTensors
  {
    torch::Tensor boundary_coords;    // [NB, 3] float32
    torch::Tensor boundary_normals;   // [NB, 3] float32
    torch::Tensor boundary_labels;    // [NB, C] float32 one-hot
    std::vector<int> zoneType_values; // length C (sorted unique)

    torch::Tensor internal_coords; // [NC, 3] float32
    torch::Tensor edge_index;      // [2, E] int64
  };

  FluentCFFGNNExporter();
  ~FluentCFFGNNExporter();

  FluentCFFGNNExporter(const FluentCFFGNNExporter&) = delete;
  FluentCFFGNNExporter& operator=(const FluentCFFGNNExporter&) = delete;

  // Ownership stays in exporter; do not delete the returned pointer.
  vtkFLUENTCFFReader* GetReader() const;

  void SetCaseFileName(std::string caseFile);
  void SetDataFileName(std::string dataFile); // optional
  void SetRenameArrays(bool renameArrays);

  // Convenience: enable all arrays before loading data.
  void EnableAllCellArrays();
  void EnableAllFaceArrays();

  // Load and Update() the underlying reader.
  // If data file is missing/unset, only case is read and field tensors will be empty.
  void Update();

  GraphTensors ExtractGraphTensors() const;

  // Concatenate all loaded cell fields into one tensor; names expanded by component.
  FieldTensor ExtractCellFieldTensor() const;

  // For boundary faces: only keep face fields that also exist as cell fields.
  FieldTensor ExtractBoundaryFieldTensor() const;

private:
  static std::vector<std::string> ExpandComponentNames(const std::string& base, int dim);

  GraphTensors ExtractGraphTensorsImpl() const;
  FieldTensor ExtractCellFieldTensorImpl() const;
  FieldTensor ExtractBoundaryFieldTensorImpl() const;

  torch::Tensor MakeFloatTensorFromFlat(const float* data, std::int64_t n, std::int64_t d) const;
  torch::Tensor MakeFloatTensorFromFlat(const std::vector<float>& data, std::int64_t d) const;

  vtkSmartPointer<vtkFLUENTCFFReader> Reader;
  std::string CaseFileName;
  std::string DataFileName;
  bool RenameArrays = false;
};
