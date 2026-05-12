#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <string>
#include <utility>
#include <vector>

#include <vtkSmartPointer.h>

#include "vtkFLUENTCFFReader.h"

namespace py = pybind11;

static const char* MODULE_DOC =
  "vtkmodules.vtkIOFLUENTCFF replaced by local vtk/IO/FLUENTCFF.\n"
  "\n"
  "This module exposes a Python-facing vtkFLUENTCFFReader wrapper for Fluent\n"
  "Common Fluids Format case/data files.";

static const char* READER_DOC =
  "Reader for Fluent Common Fluids Format files (.cas.h5 / .dat.h5).\n"
  "\n"
  "This Python wrapper exposes a focused subset of the local vtkFLUENTCFFReader API.";

class PyFluentCFFReader
{
public:
  PyFluentCFFReader()
    : Reader(vtkSmartPointer<vtkFLUENTCFFReader>::New())
  {
  }

  void SetFileName(const std::string& fileName)
  {
    this->Reader->SetFileName(fileName.c_str());
  }

  std::string GetFileName() const
  {
    return this->Reader->GetFileName();
  }

  void SetDataFileName(const std::string& dataFileName)
  {
    this->Reader->SetDataFileName(dataFileName.c_str());
  }

  std::string GetDataFileName() const
  {
    return this->Reader->GetDataFileName();
  }

  void SetRenameArrays(int renameArrays)
  {
    this->Reader->SetRenameArrays(renameArrays);
  }

  int GetRenameArrays() const
  {
    return this->Reader->GetRenameArrays();
  }

  void SetExcludedFieldArrayNames(const std::vector<std::string>& names)
  {
    this->Reader->SetExcludedFieldArrayNames(names);
  }

  void ClearExcludedFieldArrayNames()
  {
    this->Reader->ClearExcludedFieldArrayNames();
  }

  std::vector<std::string> GetExcludedFieldArrayNames() const
  {
    return this->Reader->GetExcludedFieldArrayNames();
  }

  void Update()
  {
    this->Reader->Update();
  }

  int GetNumberOfFaceZones() const
  {
    return this->Reader->GetNumberOfFaceZones();
  }

  std::string GetFaceZoneName(int index) const
  {
    const char* v = this->Reader->GetFaceZoneName(index);
    return v ? v : "";
  }

  int GetFaceZoneIdByName(const std::string& name) const
  {
    return this->Reader->GetFaceZoneIdByName(name.c_str());
  }

  int GetFaceZoneType(int zoneId) const
  {
    return this->Reader->GetFaceZoneType(zoneId);
  }

  int GetCellZoneType(int zoneId) const
  {
    return this->Reader->GetCellZoneType(zoneId);
  }

  vtkFLUENTCFFReader* GetRawReader() const
  {
    return this->Reader;
  }

private:
  vtkSmartPointer<vtkFLUENTCFFReader> Reader;
};

PYBIND11_MODULE(vtkIOFLUENTCFF, m)
{
  m.doc() = MODULE_DOC;

  py::class_<PyFluentCFFReader>(m, "vtkFLUENTCFFReader", READER_DOC)
    .def(py::init<>())
    .def("SetFileName", &PyFluentCFFReader::SetFileName,
      "Set the Fluent case file path, usually a .cas.h5 file.")
    .def("GetFileName", &PyFluentCFFReader::GetFileName,
      "Return the currently configured Fluent case file path.")
    .def("SetDataFileName", &PyFluentCFFReader::SetDataFileName,
      "Set the explicit Fluent data file path, usually a .dat.h5 file.")
    .def("GetDataFileName", &PyFluentCFFReader::GetDataFileName,
      "Return the currently configured Fluent data file path.")
    .def("SetRenameArrays", &PyFluentCFFReader::SetRenameArrays,
      "Enable or disable renaming arrays to more descriptive names.")
    .def("GetRenameArrays", &PyFluentCFFReader::GetRenameArrays,
      "Return whether array renaming is enabled.")
    .def("SetExcludedFieldArrayNames", &PyFluentCFFReader::SetExcludedFieldArrayNames,
      "Set field-array names that should be excluded during data loading.")
    .def("ClearExcludedFieldArrayNames", &PyFluentCFFReader::ClearExcludedFieldArrayNames,
      "Clear the excluded field-array name list.")
    .def("GetExcludedFieldArrayNames", &PyFluentCFFReader::GetExcludedFieldArrayNames,
      "Return the excluded field-array name list.")
    .def("Update", &PyFluentCFFReader::Update,
      "Execute the reader pipeline and load mesh/data from the configured files.")
    .def("GetNumberOfFaceZones", &PyFluentCFFReader::GetNumberOfFaceZones,
      "Return the number of currently known face zones.")
    .def("GetFaceZoneName", &PyFluentCFFReader::GetFaceZoneName,
      "Return the face-zone name for a zero-based face-zone index.")
    .def("GetFaceZoneIdByName", &PyFluentCFFReader::GetFaceZoneIdByName,
      "Return the Fluent zone id for a face-zone name.")
    .def("GetFaceZoneType", &PyFluentCFFReader::GetFaceZoneType,
      "Return the face-zone type integer for a Fluent zone id.")
    .def("GetCellZoneType", &PyFluentCFFReader::GetCellZoneType,
      "Return the cell-zone type integer for a Fluent zone id.");
}
