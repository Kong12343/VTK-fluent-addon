#include "vtkFLUENTCFFReader.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSurfaceFormat>
#include <QtConcurrent/QtConcurrent>
#include <QVBoxLayout>
#include <QVariant>
#include <QWidget>

#include <QVTKOpenGLNativeWidget.h>

#include <vtkActor.h>
#include <vtkAppendFilter.h>
#include <vtkAppendPolyData.h>
#include <vtkArrowSource.h>
#include <vtkCellData.h>
#include <vtkDataArray.h>
#include <vtkDataSet.h>
#include <vtkDataSetMapper.h>
#include <vtkDoubleArray.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkPoints.h>
#include <vtkGlyph3D.h>
#include <vtkLookupTable.h>
#include <vtkMapper.h>
#include <vtkMultiBlockDataSet.h>
#include <vtkNamedColors.h>
#include <vtkNew.h>
#include <vtkPointData.h>
#include <vtkPolyData.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>
#include <vtkUnstructuredGrid.h>

#include <algorithm>
#include <array>
#include <exception>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>

#if !defined(NDEBUG)
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#endif

namespace
{
struct LoadResult
{
  vtkSmartPointer<vtkFLUENTCFFReader> Reader;
  QString Error;
};

#if !defined(NDEBUG)
std::string ViewerDebugTimestamp()
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

void ViewerDebugLog(const std::string& message)
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

  const std::string line = "[" + ViewerDebugTimestamp() + "][FluentCFFZoneViewer][debug] " + message;
  std::cerr << line << '\n';
  if (logFile.is_open())
  {
    logFile << line << '\n';
    logFile.flush();
  }
}
#endif

enum class TopologyKind : int
{
  FaceZone = 0,
  CellZone = 1,
  FaceNormals = 2,
};

// QComboBox default item data (Qt::UserRole) holds zone face count / cell count for auto-render.
// Face zone Fluent zoneId is stored separately so the visible label may include suffixes.
constexpr int kFaceZoneIdItemRole = Qt::UserRole + 1;

void ConfigureSurfaceFormat()
{
  QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
}

int PopulateFaceZones(QComboBox* combo, vtkFLUENTCFFReader* reader, bool showBoundaryCount)
{
  combo->clear();
  int preferredIndex = -1;
  vtkIdType preferredSize = std::numeric_limits<vtkIdType>::max();
  for (int i = 0; i < reader->GetNumberOfFaceZones(); ++i)
  {
    const char* zoneName = reader->GetFaceZoneName(i);
    if (zoneName)
    {
      const vtkIdType zoneSize = reader->GetFaceZoneSizeByName(zoneName);
      const int zoneId = reader->GetFaceZoneIdByName(zoneName);
      QString displayName = QString::fromUtf8(zoneName);
      if (showBoundaryCount)
      {
        std::vector<float> tmp;
        reader->GetFaceCentroidsByZone(zoneId, tmp);
        int boundaryCount = static_cast<int>(tmp.size() / 3);
        displayName = QString("%1 (boundary:%2)").arg(displayName).arg(boundaryCount);
      }
      combo->addItem(
        displayName, QVariant::fromValue<qlonglong>(static_cast<qlonglong>(zoneSize)));
      combo->setItemData(combo->count() - 1, QVariant(zoneId), kFaceZoneIdItemRole);
      if (zoneSize > 0 && zoneSize < preferredSize)
      {
        preferredSize = zoneSize;
        preferredIndex = combo->count() - 1;
      }
    }
  }
  if (preferredIndex < 0 && combo->count() > 0)
  {
    preferredIndex = 0;
  }
  return preferredIndex;
}

int PopulateCellZones(QComboBox* combo, vtkFLUENTCFFReader* reader)
{
  combo->clear();
  int preferredIndex = -1;
  vtkIdType preferredSize = std::numeric_limits<vtkIdType>::max();
  vtkMultiBlockDataSet* mb = reader->GetOutput();
  const int n = reader->GetCellZoneCount();
  for (int i = 0; i < n; ++i)
  {
    const int zid = reader->GetCellZoneIdAtIndex(i);
    const QString label =
      QStringLiteral("Cell block %1 (zone id %2)").arg(i).arg(zid);
    vtkIdType cellCount = 0;
    if (mb && static_cast<unsigned int>(i) < mb->GetNumberOfBlocks())
    {
      if (auto* ug = vtkUnstructuredGrid::SafeDownCast(mb->GetBlock(static_cast<unsigned int>(i))))
      {
        cellCount = ug->GetNumberOfCells();
      }
    }
    combo->addItem(label, QVariant::fromValue<qlonglong>(static_cast<qlonglong>(cellCount)));
    if (cellCount > 0 && cellCount < preferredSize)
    {
      preferredSize = cellCount;
      preferredIndex = combo->count() - 1;
    }
  }
  if (preferredIndex < 0 && combo->count() > 0)
  {
    preferredIndex = 0;
  }
  return preferredIndex;
}

void PopulateFaceArrays(QComboBox* combo, vtkFLUENTCFFReader* reader)
{
  combo->clear();
  combo->addItem("Solid");
  for (int i = 0; i < reader->GetNumberOfFaceArrays(); ++i)
  {
    const char* arrayName = reader->GetFaceArrayName(i);
    if (arrayName)
    {
      combo->addItem(QString::fromUtf8(arrayName));
    }
  }
}

void PopulateCellArrays(QComboBox* combo, vtkFLUENTCFFReader* reader)
{
  combo->clear();
  combo->addItem("Solid");
  for (int i = 0; i < reader->GetNumberOfCellArrays(); ++i)
  {
    const char* arrayName = reader->GetCellArrayName(i);
    if (arrayName)
    {
      combo->addItem(QString::fromUtf8(arrayName));
    }
  }
}

void ApplyColorRange(vtkDataSet* dataSet, vtkMapper* mapper, const QString& fieldName)
{
  if (!dataSet || !dataSet->GetCellData())
  {
    mapper->ScalarVisibilityOff();
    return;
  }

  if (fieldName == QStringLiteral("Solid") || fieldName.isEmpty())
  {
    mapper->ScalarVisibilityOff();
    return;
  }

  const QByteArray utf8 = fieldName.toUtf8();
  const char* name = utf8.constData();
  vtkDataArray* arr = dataSet->GetCellData()->GetArray(name);
  if (!arr || arr->GetNumberOfTuples() == 0)
  {
    mapper->ScalarVisibilityOff();
    return;
  }

  double range[2] = { 0.0, 1.0 };
  arr->GetRange(range);
  if (!std::isfinite(range[0]) || !std::isfinite(range[1]) || range[0] > range[1])
  {
    mapper->ScalarVisibilityOff();
    return;
  }

  mapper->ScalarVisibilityOn();
  mapper->SetScalarModeToUseCellFieldData();
  mapper->SelectColorArray(name);
  mapper->SetScalarRange(range);
}
}

int main(int argc, char* argv[])
{
#if !defined(NDEBUG)
  QString executableStem = QFileInfo(QString::fromLocal8Bit((argc > 0 && argv[0]) ? argv[0] : "viewer"))
                             .completeBaseName();
  if (executableStem.isEmpty())
  {
    executableStem = QStringLiteral("FluentCFFZoneViewer");
  }
  const QString debugLogPath = executableStem + QStringLiteral("-debug.log");
  qputenv("FLUENT_CFF_DEBUG_LOG", debugLogPath.toUtf8());
  ViewerDebugLog("Debug log file: " + debugLogPath.toStdString());
#endif

  ConfigureSurfaceFormat();
  QApplication app(argc, argv);

  vtkSmartPointer<vtkFLUENTCFFReader> reader = vtkSmartPointer<vtkFLUENTCFFReader>::New();
  QFutureWatcher<LoadResult> loadWatcher;
  QElapsedTimer loadTimer;

  QWidget window;
  window.setWindowTitle("Fluent CFF Zone Viewer");

  auto* rootLayout = new QVBoxLayout(&window);

  auto* fileLayout = new QVBoxLayout();
  auto* caseLayout = new QHBoxLayout();
  auto* dataLayout = new QHBoxLayout();
  auto* controlsLayout = new QHBoxLayout();

  auto* caseLabel = new QLabel("Case");
  auto* caseEdit = new QLineEdit();
  auto* caseBrowseButton = new QPushButton("Browse");

  auto* dataLabel = new QLabel("Data");
  auto* dataEdit = new QLineEdit();
  auto* dataBrowseButton = new QPushButton("Browse");
  auto* loadButton = new QPushButton("Load");

  auto* kindLabel = new QLabel("Kind");
  auto* kindCombo = new QComboBox();
  kindCombo->addItem("Face zone");
  kindCombo->addItem("Cell zone");
  kindCombo->addItem("Face normals");

  auto* zoneLabel = new QLabel("Topology");
  auto* zoneCombo = new QComboBox();
  auto* fieldLabel = new QLabel("Color");
  auto* fieldCombo = new QComboBox();
  auto* showNormalsCheck = new QCheckBox("Show Normals");
  auto* showFullTopologyCheck = new QCheckBox(QStringLiteral("显示全拓扑"));
  auto* statusLabel = new QLabel("Ready");

  caseLayout->addWidget(caseLabel);
  caseLayout->addWidget(caseEdit, 1);
  caseLayout->addWidget(caseBrowseButton);

  dataLayout->addWidget(dataLabel);
  dataLayout->addWidget(dataEdit, 1);
  dataLayout->addWidget(dataBrowseButton);
  dataLayout->addWidget(loadButton);

  controlsLayout->addWidget(kindLabel);
  controlsLayout->addWidget(kindCombo);
  controlsLayout->addWidget(zoneLabel);
  controlsLayout->addWidget(zoneCombo, 1);
  controlsLayout->addWidget(fieldLabel);
  controlsLayout->addWidget(fieldCombo, 1);
  controlsLayout->addWidget(showNormalsCheck);
  controlsLayout->addWidget(showFullTopologyCheck);
  controlsLayout->addWidget(statusLabel);

  auto* vtkWidget = new QVTKOpenGLNativeWidget(&window);

  fileLayout->addLayout(caseLayout);
  fileLayout->addLayout(dataLayout);
  rootLayout->addLayout(fileLayout);
  rootLayout->addLayout(controlsLayout);
  rootLayout->addWidget(vtkWidget, 1);

  vtkNew<vtkGenericOpenGLRenderWindow> renderWindow;
  vtkWidget->setRenderWindow(renderWindow);

  vtkNew<vtkRenderer> renderer;
  vtkNew<vtkActor> actor;
  vtkNew<vtkDataSetMapper> mapper;
  vtkNew<vtkLookupTable> lookupTable;
  vtkNew<vtkNamedColors> colors;

  lookupTable->SetHueRange(0.667, 0.0);
  lookupTable->Build();

  mapper->SetLookupTable(lookupTable);
  actor->SetMapper(mapper);
  actor->GetProperty()->SetColor(colors->GetColor3d("Gainsboro").GetData());
  renderer->AddActor(actor);
  renderer->SetBackground(colors->GetColor3d("SlateGray").GetData());
  renderWindow->AddRenderer(renderer);

  // vtkDataSetMapper requires a connected input; keep a long-lived empty dataset for idle/clear
  // paths so the first Render() and temporary vtkSmartPointer::New() cannot drop to 0 connections.
  vtkSmartPointer<vtkPolyData> emptyDisplayDataset = vtkSmartPointer<vtkPolyData>::New();
  mapper->SetInputData(emptyDisplayDataset);
  mapper->ScalarVisibilityOff();

  const auto updateView = [&]() {
    if (!reader)
    {
      mapper->SetInputData(emptyDisplayDataset);
      mapper->ScalarVisibilityOff();
      actor->GetProperty()->SetColor(colors->GetColor3d("Gainsboro").GetData());
      zoneCombo->setEnabled(!showFullTopologyCheck->isChecked());
      renderWindow->Render();
      return;
    }

    zoneCombo->setEnabled(caseEdit->isEnabled() && !showFullTopologyCheck->isChecked());

    const QString fieldName = fieldCombo->currentText();
    const TopologyKind kind = static_cast<TopologyKind>(kindCombo->currentIndex());
    const bool fullTopology = showFullTopologyCheck->isChecked();

    if (kind == TopologyKind::FaceNormals)
    {
      vtkNew<vtkPoints> points;
      vtkNew<vtkDoubleArray> normalArray;
      normalArray->SetName("Normals");
      normalArray->SetNumberOfComponents(3);
      vtkNew<vtkDoubleArray> vectorIdx;
      vectorIdx->SetName("VectorIdx");
      vectorIdx->SetNumberOfComponents(1);

      double bMin[3] = { 0.0, 0.0, 0.0 };
      double bMax[3] = { 0.0, 0.0, 0.0 };
      int zonesWithNormals = 0;

      if (!fullTopology)
      {
        if (zoneCombo->currentIndex() < 0 || zoneCombo->count() == 0)
        {
          mapper->SetInputData(emptyDisplayDataset);
          mapper->ScalarVisibilityOff();
          actor->GetProperty()->SetColor(colors->GetColor3d("Gainsboro").GetData());
          renderWindow->Render();
          return;
        }

        const QVariant zoneIdVar = zoneCombo->currentData(kFaceZoneIdItemRole);
        const int zoneId = zoneIdVar.isValid() ? zoneIdVar.toInt() : -1;

        if (zoneId < 0)
        {
          statusLabel->setText("Zone not found");
          return;
        }

        std::vector<float> centroids;
        reader->GetFaceCentroidsByZone(zoneId, centroids);
        std::vector<float> normals;
        reader->GetFaceNormalsByZone(zoneId, normals);

        if (centroids.empty() || normals.empty())
        {
          statusLabel->setText("No face data available");
          return;
        }

        const int faceCount = static_cast<int>(centroids.size() / 3);
        statusLabel->setText(QString("Showing %1 face normals").arg(faceCount));
        if (faceCount > 0)
        {
          bMin[0] = bMax[0] = centroids[0];
          bMin[1] = bMax[1] = centroids[1];
          bMin[2] = bMax[2] = centroids[2];
        }
        for (int i = 0; i < faceCount; ++i)
        {
          const double x = centroids[i * 3];
          const double y = centroids[i * 3 + 1];
          const double z = centroids[i * 3 + 2];
          points->InsertNextPoint(x, y, z);
          normalArray->InsertNextTuple3(static_cast<double>(normals[i * 3]),
            static_cast<double>(normals[i * 3 + 1]), static_cast<double>(normals[i * 3 + 2]));
          vectorIdx->InsertNextValue(static_cast<double>(i));
          bMin[0] = std::min(bMin[0], x);
          bMin[1] = std::min(bMin[1], y);
          bMin[2] = std::min(bMin[2], z);
          bMax[0] = std::max(bMax[0], x);
          bMax[1] = std::max(bMax[1], y);
          bMax[2] = std::max(bMax[2], z);
        }
      }
      else
      {
        if (reader->GetNumberOfFaceZones() <= 0)
        {
          mapper->SetInputData(emptyDisplayDataset);
          mapper->ScalarVisibilityOff();
          actor->GetProperty()->SetColor(colors->GetColor3d("Gainsboro").GetData());
          renderWindow->Render();
          return;
        }

        int globalIdx = 0;
        bool boundsInit = false;
        for (int zi = 0; zi < reader->GetNumberOfFaceZones(); ++zi)
        {
          const char* zn = reader->GetFaceZoneName(zi);
          if (!zn)
          {
            continue;
          }
          const int zoneId = reader->GetFaceZoneIdByName(zn);
          if (zoneId < 0)
          {
            continue;
          }
          std::vector<float> centroids;
          reader->GetFaceCentroidsByZone(zoneId, centroids);
          std::vector<float> normals;
          reader->GetFaceNormalsByZone(zoneId, normals);
          if (centroids.empty() || normals.empty())
          {
            continue;
          }
          ++zonesWithNormals;
          const int fc = static_cast<int>(centroids.size() / 3);
          for (int i = 0; i < fc; ++i)
          {
            const double x = centroids[i * 3];
            const double y = centroids[i * 3 + 1];
            const double z = centroids[i * 3 + 2];
            points->InsertNextPoint(x, y, z);
            normalArray->InsertNextTuple3(static_cast<double>(normals[i * 3]),
              static_cast<double>(normals[i * 3 + 1]), static_cast<double>(normals[i * 3 + 2]));
            vectorIdx->InsertNextValue(static_cast<double>(globalIdx++));
            if (!boundsInit)
            {
              bMin[0] = bMax[0] = x;
              bMin[1] = bMax[1] = y;
              bMin[2] = bMax[2] = z;
              boundsInit = true;
            }
            else
            {
              bMin[0] = std::min(bMin[0], x);
              bMin[1] = std::min(bMin[1], y);
              bMin[2] = std::min(bMin[2], z);
              bMax[0] = std::max(bMax[0], x);
              bMax[1] = std::max(bMax[1], y);
              bMax[2] = std::max(bMax[2], z);
            }
          }
        }

        if (points->GetNumberOfPoints() == 0)
        {
          statusLabel->setText("No face data available (full topology)");
          mapper->SetInputData(emptyDisplayDataset);
          mapper->ScalarVisibilityOff();
          actor->GetProperty()->SetColor(colors->GetColor3d("Gainsboro").GetData());
          renderWindow->Render();
          return;
        }
        statusLabel->setText(QStringLiteral("全拓扑: %1 个采样点（%2 个有边界数据的 face zone）")
                               .arg(points->GetNumberOfPoints())
                               .arg(zonesWithNormals));
      }

      const int faceCount = points->GetNumberOfPoints();
      if (faceCount == 0)
      {
        mapper->SetInputData(emptyDisplayDataset);
        mapper->ScalarVisibilityOff();
        actor->GetProperty()->SetColor(colors->GetColor3d("Gainsboro").GetData());
        renderWindow->Render();
        return;
      }

      const double dx = bMax[0] - bMin[0];
      const double dy = bMax[1] - bMin[1];
      const double dz = bMax[2] - bMin[2];
      const double diag = std::sqrt(dx * dx + dy * dy + dz * dz);
      const double arrowLength = std::max(diag * 0.05, 1e-6);

      vtkNew<vtkPolyData> normalsPolyData;
      normalsPolyData->SetPoints(points);
      normalsPolyData->GetPointData()->SetNormals(normalArray);
      normalsPolyData->GetPointData()->SetScalars(vectorIdx);

      const double idxMax = static_cast<double>(std::max(faceCount - 1, 1));
      lookupTable->SetHueRange(0.667, 0.0);
      lookupTable->SetTableRange(0.0, idxMax);
      lookupTable->Build();
      mapper->SetLookupTable(lookupTable);
      mapper->SetScalarRange(0.0, idxMax);

      if (showNormalsCheck->isChecked())
      {
        vtkNew<vtkArrowSource> arrowSource;
        arrowSource->SetTipResolution(14);
        arrowSource->SetShaftResolution(10);
        arrowSource->SetTipLength(0.35);
        arrowSource->SetTipRadius(0.11);
        arrowSource->SetShaftRadius(0.04);
        arrowSource->Update();

        vtkNew<vtkGlyph3D> glyph;
        glyph->SetSourceData(arrowSource->GetOutput());
        glyph->SetInputData(normalsPolyData);
        glyph->OrientOn();
        glyph->SetVectorModeToUseNormal();
        glyph->SetScaleModeToScaleByVector();
        glyph->SetScaleFactor(arrowLength);
        glyph->SetColorModeToColorByScalar();
        glyph->Update();

        mapper->SetInputConnection(glyph->GetOutputPort());
        mapper->ScalarVisibilityOn();
        mapper->SetScalarModeToUseCellData();
        actor->GetProperty()->SetRepresentationToSurface();
        actor->GetProperty()->SetLineWidth(1.0);
      }
      else
      {
        mapper->SetInputData(normalsPolyData);
        mapper->ScalarVisibilityOn();
        mapper->SetScalarModeToUsePointData();
        mapper->SelectColorArray("VectorIdx");
        actor->GetProperty()->SetRepresentationToPoints();
        actor->GetProperty()->SetPointSize(8.0f);
      }

      renderWindow->Render();
      return;
    }
    else if (kind == TopologyKind::FaceZone)
    {
      const QByteArray fieldUtf8 = (fieldName == QStringLiteral("Solid") || fieldName.isEmpty())
        ? QByteArray()
        : fieldName.toUtf8();
      const char* faceArrayName = fieldUtf8.isEmpty() ? nullptr : fieldUtf8.constData();

      vtkSmartPointer<vtkPolyData> facePd;

      if (fullTopology)
      {
        vtkNew<vtkAppendPolyData> appendPd;
        int zonesUsed = 0;
        for (int zi = 0; zi < reader->GetNumberOfFaceZones(); ++zi)
        {
          const char* zn = reader->GetFaceZoneName(zi);
          if (!zn)
          {
            continue;
          }
          vtkSmartPointer<vtkPolyData> zpd = reader->CreateFaceZonePolyData(zn, faceArrayName, 0);
          if (zpd && zpd->GetNumberOfCells() > 0)
          {
            appendPd->AddInputData(zpd);
            ++zonesUsed;
          }
        }
        appendPd->Update();
        facePd = appendPd->GetOutput();
        if (!facePd || facePd->GetNumberOfCells() == 0)
        {
          statusLabel->setText(QStringLiteral("无 face zone 几何（全拓扑）"));
          mapper->SetInputData(emptyDisplayDataset);
          mapper->ScalarVisibilityOff();
          actor->GetProperty()->SetColor(colors->GetColor3d("Gainsboro").GetData());
          renderWindow->Render();
          return;
        }
        statusLabel->setText(QStringLiteral("全拓扑 Face zone: %1 个 zone 已合并").arg(zonesUsed));
      }
      else
      {
        const int zoneIndex = zoneCombo->currentIndex();
        if (zoneIndex < 0 || zoneIndex >= reader->GetNumberOfFaceZones())
        {
          mapper->SetInputData(emptyDisplayDataset);
          mapper->ScalarVisibilityOff();
          actor->GetProperty()->SetColor(colors->GetColor3d("Gainsboro").GetData());
          renderWindow->Render();
          return;
        }

        const char* zoneName = reader->GetFaceZoneName(zoneIndex);
        if (!zoneName)
        {
          mapper->SetInputData(emptyDisplayDataset);
          mapper->ScalarVisibilityOff();
          actor->GetProperty()->SetColor(colors->GetColor3d("Gainsboro").GetData());
          renderWindow->Render();
          return;
        }

        facePd = reader->CreateFaceZonePolyData(zoneName, faceArrayName, 0);
        if (!facePd || facePd->GetNumberOfCells() == 0)
        {
          statusLabel->setText("No face zone geometry");
          mapper->SetInputData(emptyDisplayDataset);
          mapper->ScalarVisibilityOff();
          actor->GetProperty()->SetColor(colors->GetColor3d("Gainsboro").GetData());
          renderWindow->Render();
          return;
        }
      }

      mapper->SetInputData(facePd);
      actor->GetProperty()->SetRepresentationToSurface();
      actor->GetProperty()->SetPointSize(1);
      ApplyColorRange(facePd, mapper, fieldName);
    }
    else
    {
      vtkMultiBlockDataSet* mb = reader->GetOutput();
      vtkUnstructuredGrid* displayUg = nullptr;

      if (fullTopology)
      {
        const int nBlocks = reader->GetCellZoneCount();
        if (!mb || nBlocks <= 0)
        {
          mapper->SetInputData(emptyDisplayDataset);
          mapper->ScalarVisibilityOff();
          actor->GetProperty()->SetColor(colors->GetColor3d("Gainsboro").GetData());
          renderWindow->Render();
          return;
        }

        vtkNew<vtkAppendFilter> appendUg;
        int blocksUsed = 0;
        for (int bi = 0; bi < nBlocks; ++bi)
        {
          auto* ug = vtkUnstructuredGrid::SafeDownCast(mb->GetBlock(static_cast<unsigned int>(bi)));
          if (ug && ug->GetNumberOfCells() > 0)
          {
            appendUg->AddInputData(ug);
            ++blocksUsed;
          }
        }
        appendUg->Update();
        displayUg = appendUg->GetOutput();
        if (!displayUg || displayUg->GetNumberOfCells() == 0)
        {
          statusLabel->setText(QStringLiteral("无 cell zone 块（全拓扑）"));
          mapper->SetInputData(emptyDisplayDataset);
          mapper->ScalarVisibilityOff();
          actor->GetProperty()->SetColor(colors->GetColor3d("Gainsboro").GetData());
          renderWindow->Render();
          return;
        }
        statusLabel->setText(QStringLiteral("全拓扑 Cell zone: %1 个 block 已合并").arg(blocksUsed));
#if !defined(NDEBUG)
        ViewerDebugLog("Cell full topology cells=" + std::to_string(displayUg->GetNumberOfCells()) +
          " points=" + std::to_string(displayUg->GetNumberOfPoints()));
#endif
      }
      else
      {
        const int blockIndex = zoneCombo->currentIndex();
        if (blockIndex < 0 || blockIndex >= reader->GetCellZoneCount())
        {
          mapper->SetInputData(emptyDisplayDataset);
          mapper->ScalarVisibilityOff();
          actor->GetProperty()->SetColor(colors->GetColor3d("Gainsboro").GetData());
          renderWindow->Render();
          return;
        }

        if (!mb || static_cast<unsigned int>(blockIndex) >= mb->GetNumberOfBlocks())
        {
          mapper->SetInputData(emptyDisplayDataset);
          mapper->ScalarVisibilityOff();
          actor->GetProperty()->SetColor(colors->GetColor3d("Gainsboro").GetData());
          renderWindow->Render();
          return;
        }

        displayUg =
          vtkUnstructuredGrid::SafeDownCast(mb->GetBlock(static_cast<unsigned int>(blockIndex)));
        if (!displayUg)
        {
          mapper->SetInputData(emptyDisplayDataset);
          mapper->ScalarVisibilityOff();
          actor->GetProperty()->SetColor(colors->GetColor3d("Gainsboro").GetData());
          renderWindow->Render();
          return;
        }
#if !defined(NDEBUG)
        {
          vtkIdType nCells = displayUg->GetNumberOfCells();
          vtkIdType nPts = displayUg->GetNumberOfPoints();
          std::string info = "Cell zone block " + std::to_string(blockIndex) +
            " cells=" + std::to_string(nCells) +
            " points=" + std::to_string(nPts);
          if (nCells > 0 && nCells <= 500000)
          {
            std::array<vtkIdType, 16> tc{};
            for (vtkIdType ci = 0; ci < nCells; ++ci)
            {
              int ct = displayUg->GetCellType(ci);
              if (ct >= 0 && ct < 16) tc[static_cast<std::size_t>(ct)]++;
              else tc[15]++;
            }
            for (int t = 0; t < 16; ++t)
              if (tc[static_cast<std::size_t>(t)] > 0)
                info += " VTK_" + std::to_string(t) + "=" + std::to_string(tc[static_cast<std::size_t>(t)]);
          }
          ViewerDebugLog(info);
        }
#endif
      }

      mapper->SetInputData(displayUg);
      actor->GetProperty()->SetRepresentationToSurface();
      actor->GetProperty()->SetPointSize(1);
      ApplyColorRange(displayUg, mapper, fieldName);
    }

    if (fieldName == QStringLiteral("Solid") || !mapper->GetScalarVisibility())
    {
      actor->GetProperty()->SetColor(colors->GetColor3d("Gainsboro").GetData());
    }
    renderer->ResetCamera();
    renderWindow->Render();
  };

  const auto setLoadingState = [&](bool isLoading, const QString& text) {
    caseEdit->setEnabled(!isLoading);
    dataEdit->setEnabled(!isLoading);
    caseBrowseButton->setEnabled(!isLoading);
    dataBrowseButton->setEnabled(!isLoading);
    loadButton->setEnabled(!isLoading);
    kindCombo->setEnabled(!isLoading);
    zoneCombo->setEnabled(!isLoading && !showFullTopologyCheck->isChecked());
    fieldCombo->setEnabled(!isLoading);
    showNormalsCheck->setEnabled(!isLoading);
    showFullTopologyCheck->setEnabled(!isLoading);
    statusLabel->setText(text);
  };

  const auto loadFiles = [&]() {
    if (loadWatcher.isRunning())
    {
      return;
    }

    const QString casePath = caseEdit->text().trimmed();
    const QString dataPath = dataEdit->text().trimmed();
    if (casePath.isEmpty())
    {
      QMessageBox::warning(&window, "Missing Case", "Please choose a .cas.h5 file.");
      return;
    }

    setLoadingState(true, "Loading...");
    loadTimer.restart();
    loadWatcher.setFuture(QtConcurrent::run([casePath, dataPath]() {
      LoadResult result;
      try
      {
        result.Reader = vtkSmartPointer<vtkFLUENTCFFReader>::New();
        result.Reader->SetFileName(casePath.toUtf8().constData());
        if (dataPath.isEmpty())
        {
          result.Reader->SetDataFileName("");
        }
        else
        {
          result.Reader->SetDataFileName(dataPath.toUtf8().constData());
        }
#if !defined(NDEBUG)
        {
          const auto t0 = std::chrono::steady_clock::now();
          result.Reader->UpdateInformation();
          const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0)
                            .count();
          ViewerDebugLog("UpdateInformation " + std::to_string(ms) + " ms");
        }
        {
          const auto t0 = std::chrono::steady_clock::now();
          result.Reader->EnableAllCellArrays();
          result.Reader->EnableAllFaceArrays();
          const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0)
                            .count();
          ViewerDebugLog(
            "EnableAllCellArrays+EnableAllFaceArrays " + std::to_string(ms) + " ms");
        }
        {
          const auto t0 = std::chrono::steady_clock::now();
          result.Reader->Update();
          const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0)
                            .count();
          ViewerDebugLog("Update " + std::to_string(ms) + " ms");
        }
#else
        result.Reader->UpdateInformation();
        result.Reader->EnableAllCellArrays();
        result.Reader->EnableAllFaceArrays();
        result.Reader->Update();
#endif
      }
      catch (const std::exception& ex)
      {
        result.Error = QString::fromUtf8(ex.what());
      }
      catch (...)
      {
        result.Error = "Reader threw an unknown exception while loading the files.";
      }
      return result;
    }));
  };

  QObject::connect(&loadWatcher, &QFutureWatcher<LoadResult>::finished, &window, [&]() {
    vtkIdType kAutoRenderComplexityLimit = static_cast<vtkIdType>(200000);
#if !defined(NDEBUG)
    kAutoRenderComplexityLimit = std::numeric_limits<vtkIdType>::max();
#endif
    if (const char* limEnv = std::getenv("FLUENT_CFF_AUTO_RENDER_LIMIT"))
    {
      char* end = nullptr;
      const unsigned long parsed = std::strtoul(limEnv, &end, 10);
      if (end != limEnv && *end == '\0')
      {
        if (parsed == 0UL)
        {
          kAutoRenderComplexityLimit = std::numeric_limits<vtkIdType>::max();
        }
        else
        {
          const unsigned long cap = static_cast<unsigned long>(std::numeric_limits<vtkIdType>::max());
          kAutoRenderComplexityLimit = static_cast<vtkIdType>(std::min(parsed, cap));
        }
      }
    }
    const LoadResult result = loadWatcher.result();
    if (!result.Error.isEmpty() || !result.Reader)
    {
      setLoadingState(false, "Load failed");
      QMessageBox::critical(&window, "Load Failed",
        result.Error.isEmpty() ? "Reader failed to load the selected files." : result.Error);
      return;
    }

    reader = result.Reader;
    int preferredZoneIndex = -1;
    {
      QSignalBlocker zoneBlocker(zoneCombo);
      QSignalBlocker fieldBlocker(fieldCombo);
      const TopologyKind kind = static_cast<TopologyKind>(kindCombo->currentIndex());
      if (kind == TopologyKind::FaceZone || kind == TopologyKind::FaceNormals)
      {
        preferredZoneIndex = PopulateFaceZones(zoneCombo, reader, kind == TopologyKind::FaceNormals);
        if (kind == TopologyKind::FaceZone)
        {
          PopulateFaceArrays(fieldCombo, reader);
        }
      }
      else if (kind == TopologyKind::CellZone)
      {
        preferredZoneIndex = PopulateCellZones(zoneCombo, reader);
        PopulateCellArrays(fieldCombo, reader);
      }
      if (preferredZoneIndex >= 0)
      {
        zoneCombo->setCurrentIndex(preferredZoneIndex);
      }
      if (fieldCombo->count() > 0)
      {
        fieldCombo->setCurrentIndex(0);
      }
    }

    bool rendered = false;
    if (preferredZoneIndex >= 0)
    {
      const vtkIdType complexity =
        static_cast<vtkIdType>(zoneCombo->itemData(preferredZoneIndex).toLongLong());
#if !defined(NDEBUG)
      ViewerDebugLog("Auto-render check: preferred topology complexity=" + std::to_string(complexity) +
        " limit=" + std::to_string(static_cast<long long>(kAutoRenderComplexityLimit)) +
        (kAutoRenderComplexityLimit == std::numeric_limits<vtkIdType>::max() ? " (uncapped)" : ""));
#endif
      if (complexity > 0 && complexity <= kAutoRenderComplexityLimit)
      {
        statusLabel->setText("Drawing...");
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        updateView();
        rendered = true;
      }
#if !defined(NDEBUG)
      else if (complexity > kAutoRenderComplexityLimit)
      {
        ViewerDebugLog("Auto-render skipped (complexity exceeds limit); pick another topology or set "
                       "FLUENT_CFF_AUTO_RENDER_LIMIT=0 to uncap");
      }
#endif
    }

    if (rendered)
    {
      setLoadingState(
        false, QString("Loaded in %1 s").arg(loadTimer.elapsed() / 1000.0, 0, 'f', 1));
    }
    else
    {
      setLoadingState(false, QString("Loaded in %1 s, choose a topology to display")
                             .arg(loadTimer.elapsed() / 1000.0, 0, 'f', 1));
    }
  });

  QObject::connect(kindCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), &window,
    [&](int) {
      if (!reader)
      {
        return;
      }
      int preferredZoneIndex = -1;
      {
        QSignalBlocker zoneBlocker(zoneCombo);
        QSignalBlocker fieldBlocker(fieldCombo);
        const TopologyKind kind = static_cast<TopologyKind>(kindCombo->currentIndex());
        if (kind == TopologyKind::FaceZone || kind == TopologyKind::FaceNormals)
        {
          preferredZoneIndex = PopulateFaceZones(zoneCombo, reader, kind == TopologyKind::FaceNormals);
          if (kind == TopologyKind::FaceZone)
          {
            PopulateFaceArrays(fieldCombo, reader);
          }
        }
        else if (kind == TopologyKind::CellZone)
        {
          preferredZoneIndex = PopulateCellZones(zoneCombo, reader);
          PopulateCellArrays(fieldCombo, reader);
        }
        if (preferredZoneIndex >= 0)
        {
          zoneCombo->setCurrentIndex(preferredZoneIndex);
        }
        else if (zoneCombo->count() > 0)
        {
          zoneCombo->setCurrentIndex(0);
        }
        if (fieldCombo->count() > 0)
        {
          fieldCombo->setCurrentIndex(0);
        }
      }
      updateView();
    });

  QObject::connect(zoneCombo, &QComboBox::currentTextChanged, &window, [&](const QString&) {
    updateView();
  });
  QObject::connect(fieldCombo, &QComboBox::currentTextChanged, &window, [&](const QString&) {
    updateView();
  });
  QObject::connect(loadButton, &QPushButton::clicked, &window, [&]() {
    loadFiles();
  });
  QObject::connect(caseBrowseButton, &QPushButton::clicked, &window, [&]() {
    const QString path = QFileDialog::getOpenFileName(
      &window, "Select case file", caseEdit->text(), "Fluent Case (*.cas.h5)");
    if (!path.isEmpty())
    {
      caseEdit->setText(path);
      if (dataEdit->text().trimmed().isEmpty())
      {
        const QString candidate = path.left(path.size() - QString("cas.h5").size()) + "dat.h5";
        if (QFileInfo::exists(candidate))
        {
          dataEdit->setText(candidate);
        }
      }
    }
  });
  QObject::connect(dataBrowseButton, &QPushButton::clicked, &window, [&]() {
    const QString path = QFileDialog::getOpenFileName(
      &window, "Select data file", dataEdit->text(), "Fluent Data (*.dat.h5)");
    if (!path.isEmpty())
    {
      dataEdit->setText(path);
    }
  });

  if (argc >= 2)
  {
    caseEdit->setText(QString::fromUtf8(argv[1]));
  }
  if (argc >= 3)
  {
    dataEdit->setText(QString::fromUtf8(argv[2]));
  }
  if (!caseEdit->text().trimmed().isEmpty())
  {
    loadFiles();
  }

  QObject::connect(showNormalsCheck, &QCheckBox::toggled, &window, [&](bool) {
    updateView();
  });
  QObject::connect(showFullTopologyCheck, &QCheckBox::toggled, &window, [&](bool) {
    if (reader)
    {
      zoneCombo->setEnabled(caseEdit->isEnabled() && !showFullTopologyCheck->isChecked());
    }
    else
    {
      zoneCombo->setEnabled(!showFullTopologyCheck->isChecked());
    }
    updateView();
  });

  window.resize(1480, 960);
  window.show();

  return app.exec();
}
