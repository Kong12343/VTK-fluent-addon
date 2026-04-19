// Keep this in a dedicated TU to ensure explicit instantiation is present when building
// without VTK extern templates on MinGW.

#include "vtkAOSDataArrayTemplate.h"

template class vtkAOSDataArrayTemplate<double>;

