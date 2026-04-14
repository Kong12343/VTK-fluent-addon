#include "vtkFLUENTCFFReader.h"
#include <vtkNew.h>
#include <chrono>
#include <iostream>

int main(int argc, char* argv[])
{
  if (argc < 2)
  {
    std::cerr << "usage: reader_probe <case> [dat]\n";
    return 2;
  }

  vtkNew<vtkFLUENTCFFReader> reader;
  reader->SetFileName(argv[1]);
  if (argc >= 3)
  {
    reader->SetDataFileName(argv[2]);
  }

  auto t0 = std::chrono::steady_clock::now();
  std::cout << "before UpdateInformation" << std::endl;
  reader->UpdateInformation();
  auto t1 = std::chrono::steady_clock::now();
  std::cout << "after UpdateInformation ms="
            << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()
            << std::endl;

  std::cout << "face zones=" << reader->GetNumberOfFaceZones() << std::endl;
  std::cout << "face arrays=" << reader->GetNumberOfFaceArrays() << std::endl;

  reader->EnableAllCellArrays();
  reader->EnableAllFaceArrays();

  std::cout << "before Update" << std::endl;
  reader->Update();
  auto t2 = std::chrono::steady_clock::now();
  std::cout << "after Update ms="
            << std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count()
            << std::endl;

  std::cout << "done" << std::endl;
  return 0;
}