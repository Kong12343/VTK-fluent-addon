#include "vtkFLUENTCFFReader.h"

#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSurfaceFormat>
#include <QtConcurrent/QtConcurrent>
#include <QVBoxLayout>
#include <QWidget>

#include <QVTKOpenGLNativeWidget.h>

#include <vtkActor.h>
#include <vtkCellData.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkLookupTable.h>
#include <vtkNamedColors.h>
#include <vtkNew.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>

#include <exception>
#include <string>

namespace
{
struct LoadResult
{
  vtkSmartPointer<vtkFLUENTCFFReader> Reader;
  QString Error;
};

void ConfigureSurfaceFormat()
{
  QSurfaceFormat::setDefaultFormat(QVTKOpenGLNativeWidget::defaultFormat());
}

void PopulateFaceZones(QComboBox* combo, vtkFLUENTCFFReader* reader)
{
  combo->clear();
  for (int i = 0; i < reader->GetNumberOfFaceZones(); ++i)
  {
    const char* zoneName = reader->GetFaceZoneName(i);
    if (zoneName)
    {
      combo->addItem(QString::fromUtf8(zoneName));
    }
  }
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

void ApplyColorRange(vtkPolyData* polyData, vtkPolyDataMapper* mapper)
{
  if (!polyData || !polyData->GetCellData() || !polyData->GetCellData()->GetScalars())
  {
    mapper->ScalarVisibilityOff();
    return;
  }

  double range[2] = { 0.0, 1.0 };
  polyData->GetCellData()->GetScalars()->GetRange(range);
  mapper->ScalarVisibilityOn();
  mapper->SetScalarModeToUseCellData();
  mapper->SetScalarRange(range);
}
}

int main(int argc, char* argv[])
{
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

  auto* zoneLabel = new QLabel("Topology");
  auto* zoneCombo = new QComboBox();
  auto* fieldLabel = new QLabel("Color");
  auto* fieldCombo = new QComboBox();
  auto* statusLabel = new QLabel("Ready");

  caseLayout->addWidget(caseLabel);
  caseLayout->addWidget(caseEdit, 1);
  caseLayout->addWidget(caseBrowseButton);

  dataLayout->addWidget(dataLabel);
  dataLayout->addWidget(dataEdit, 1);
  dataLayout->addWidget(dataBrowseButton);
  dataLayout->addWidget(loadButton);

  controlsLayout->addWidget(zoneLabel);
  controlsLayout->addWidget(zoneCombo, 1);
  controlsLayout->addWidget(fieldLabel);
  controlsLayout->addWidget(fieldCombo, 1);
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
  vtkNew<vtkPolyDataMapper> mapper;
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

  const auto updateView = [&]() {
    if (!reader)
    {
      return;
    }

    const QString zoneName = zoneCombo->currentText();
    if (zoneName.isEmpty())
    {
      mapper->SetInputData(vtkSmartPointer<vtkPolyData>::New());
      renderWindow->Render();
      return;
    }

    const QString fieldName = fieldCombo->currentText();
    const QByteArray zoneUtf8 = zoneName.toUtf8();
    const QByteArray fieldUtf8 = fieldName.toUtf8();
    const char* colorArray = fieldName == "Solid" ? nullptr : fieldUtf8.constData();

    vtkSmartPointer<vtkPolyData> polyData =
      reader->CreateFaceZonePolyData(zoneUtf8.constData(), colorArray, 0);

    mapper->SetInputData(polyData);
    ApplyColorRange(polyData, mapper);
    if (!colorArray)
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
    zoneCombo->setEnabled(!isLoading);
    fieldCombo->setEnabled(!isLoading);
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
        result.Reader->UpdateInformation();
        result.Reader->EnableAllCellArrays();
        result.Reader->EnableAllFaceArrays();
        result.Reader->Update();
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
    const LoadResult result = loadWatcher.result();
    if (!result.Error.isEmpty() || !result.Reader)
    {
      setLoadingState(false, "Load failed");
      QMessageBox::critical(&window, "Load Failed",
        result.Error.isEmpty() ? "Reader failed to load the selected files." : result.Error);
      return;
    }

    reader = result.Reader;
    PopulateFaceZones(zoneCombo, reader);
    PopulateFaceArrays(fieldCombo, reader);
    if (zoneCombo->count() > 0)
    {
      zoneCombo->setCurrentIndex(0);
    }
    if (fieldCombo->count() > 0)
    {
      fieldCombo->setCurrentIndex(0);
    }
    updateView();
    setLoadingState(false,
      QString("Loaded in %1 s").arg(loadTimer.elapsed() / 1000.0, 0, 'f', 1));
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

  window.resize(1480, 960);
  window.show();

  return app.exec();
}