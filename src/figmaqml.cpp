#include "figmadocument.h"
#include "figmaparser.h"
#include "figmaqml.h"
#include "fontcache.h"
#include <QVersionNumber>
#include <QTimer>
#include <QFile>
#include <QSize>
#include <QQmlEngine>
#include <QDir>
#include <QFontDatabase>
#include <QFontInfo>
#include <QStandardPaths>
#include <QEventLoop>
#include <exception>

#ifdef WASM_FILEDIALOGS
#include <QTemporaryDir>
#include <QFileDialog>
#endif

#ifndef NO_CONCURRENT
#include <QtConcurrent>
template <class T> using Future = QFuture<T>;
namespace Concurrent = QtConcurrent;
template <class T> using FutureWatcher = QFutureWatcher<T>;
#else
//#include "NonConcurrent.hpp"
//template <class T> using Future = NonConcurrent::Future<T>;
//namespace Concurrent = NonConcurrent;
//template <class T> using FutureWatcher = NonConcurrent::FutureWatcher<T>;
#endif

#include <QTime>
#define TIMED_START(s)  const auto s = QTime::currentTime();
#define TIMED_END(s, p) if(m_flags & Timed ) {emit info(toStr("timed", p, s.msecsTo(QTime::currentTime())));}

class RAII_ {
public:
    using Deldelegate = std::function<void()>;
    RAII_(const Deldelegate& d) : m_d(d) {}
    ~RAII_() {m_d();}
private:
    Deldelegate m_d;
};

#define SCAT(a, b) a ## b
#define SCAT2(a, b) SCAT(a, b)
#define RAII(x) RAII_ SCAT2(raii, __LINE__)(x)

using namespace std::chrono_literals;

const QLatin1String qmlViewPath("/qml/");
const QLatin1String sourceViewPath("/sources/");
const QLatin1String Images("/images/");
const QLatin1String FileHeader("//Generated by FigmaQML\n\n");

static int levenshteinDistance(const QString& s1, const QString& s2) {
    const auto l1 = s1.length();
    const auto l2 = s2.length();

    auto dist = std::vector<std::vector<int>>(l2 + 1, std::vector<int>(l1 + 1));


    for(auto i = 0; i <= l1 ; i++) {
       dist[0][i] = i;
    }

    for(auto j = 0; j <= l2; j++) {
       dist[j][0] = j;
    }
    for (auto j = 1; j <= l1; j++) {
       for(auto i = 1; i <= l2 ;i++) {
          const auto track = (s2[i-1] == s1[j-1]) ? 0 : 1;
          const auto t = std::min((dist[i - 1][j] + 1), (dist[i][j - 1] + 1));
          dist[i][j] = std::min(t, (dist[i - 1][j - 1] + track));
       }
    }
    return dist[l2][l1];
}

enum Format {
    None = 0, JPEG, PNG
};

FigmaQml::~FigmaQml() {
}

int FigmaQml::canvasCount() const {
    return m_uiDoc ? m_uiDoc->size() : 0;
}

int FigmaQml::elementCount() const {
    return m_uiDoc && canvasCount() > 0 ? m_uiDoc->current().size() : 0;
}

int FigmaQml::currentElement() const {
    return (m_uiDoc && !m_uiDoc->empty()) ? m_uiDoc->current().currentIndex() : 0;
}

int FigmaQml::currentCanvas() const {
    return m_uiDoc ? m_uiDoc->currentIndex() : 0;
}

QString FigmaQml::canvasName() const {
    return m_uiDoc ? m_uiDoc->current().name() : "";
}

QString FigmaQml::documentName() const {
    return m_uiDoc ? m_uiDoc->name() : "";
}

QString FigmaQml::qmlDir() const {
    return m_qmlDir;
}

QString FigmaQml::elementName() const {
    return (m_uiDoc && !m_uiDoc->empty()) ? m_uiDoc->current().name(currentElement()) : QString();
}

QString FigmaQml::documentsLocation() const {
    return QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
}

const auto FrameDelay = 500ms;

bool FigmaQml::setCurrentElement(int current) {
    if(current < 0 || current >= elementCount())
        return false;
    if(current != currentElement()) {
        m_uiDoc->getCurrent()->setCurrent(current);
        emit elementNameChanged();
        QTimer::singleShot(FrameDelay, this, [this](){emit currentElementChanged();}); //delayed
    }
    return true;
}

bool FigmaQml::setCurrentCanvas(int current) {
    if(current < 0 || current >= canvasCount())
        return false;
    if(current != currentCanvas()) {
        m_uiDoc->setCurrent(current);
        if(m_uiDoc->currentIndex() >= m_uiDoc->current().size()) {
            m_uiDoc->getCurrent()->setCurrent(m_uiDoc->current().size() - 1);
        }
        emit currentCanvasChanged();
        emit elementNameChanged();
        emit elementCountChanged();
        QTimer::singleShot(FrameDelay, this, [this](){emit currentElementChanged();}); //delayed
    }
    return true;
}



QString FigmaQml::validFileName(const QString& name) {
   return FigmaParser::validFileName(name);
}

bool FigmaQml::saveAllQML(const QString& folderName) const {
#ifdef Q_OS_WINDOWS
    QDir d(folderName.startsWith('/') ? folderName.mid(1) : folderName);
#else
    QDir d(folderName);
#endif
    if(!ensureDirExists(d.absolutePath())) {
        return false;
    }
    QSet<QString> componentNames;
    for(const auto& c : *m_sourceDoc) {
        for(const auto& e : *c) {
            const auto fullname = QString("%1/%2_%3.qml").arg(d.absolutePath()).arg(validFileName(c->name())).arg(e->name());
            QFile file(fullname);
            if(!file.open(QIODevice::WriteOnly)) {
                emit error(QString("Failed to write %1 %2 %3 %4").arg(file.errorString(), fullname, d.absolutePath(), e->name()));
                return false;
            }
            if(e->data().length() == 0 || file.write(e->data()) < 0) {
                emit error(QString("Failed to write %1 %2 %3 %4").arg(file.errorString(), fullname, d.absolutePath(), e->name()));
                return false;
            }
            const auto elementComponents = m_sourceDoc->components(e->name());
            componentNames.unite(QSet(elementComponents.begin(), elementComponents.end()));
        }
    }

    for(const auto& componentName : componentNames) {
        Q_ASSERT(componentName.endsWith(FIGMA_SUFFIX));
        const auto fullname = QString("%1/%2.qml").arg(d.absolutePath()).arg(componentName);
        QFile file(fullname);
        if(!file.open(QIODevice::WriteOnly)) {
            emit error(QString("Failed to write \"%1\" \"%2\" \"%3\" \"%4\"").arg(file.errorString(), fullname, d.absolutePath(), componentName));
            return false;
        }

        if(!m_sourceDoc->containsComponent(componentName)) {
            emit error(QString("Failed to find \"%1\" on write").arg(componentName));
            return false;
        }

        const auto cd = m_sourceDoc->component(componentName);
        if(cd.length() == 0 || file.write(cd) < 0) {
            emit error(QString("Failed to write \"%1\" \"%2\" \"%3\" \"%4\"").arg(file.errorString(), fullname, d.absolutePath(), componentName));
            return false;
        }
    }

    if(!saveImages(d.absolutePath() + Images))
        return false;
    emit info(QString("%1 files written into %2").arg(m_imageFiles.size() + componentNames.count()
                                                      + std::accumulate(m_sourceDoc->begin(), m_sourceDoc->end(), 0, [](const auto &a, const auto& c){return a + c->size();}))
              .arg(d.absolutePath()));
    return true;
}

QUrl FigmaQml::element() const {
      return (m_uiDoc && !m_uiDoc->empty()) ?  QUrl::fromLocalFile(QString(m_uiDoc->current().current())) : QUrl();
}

QByteArray FigmaQml::sourceCode() const {
      return (m_sourceDoc && !m_sourceDoc->empty()) ?  m_sourceDoc->current().current() : QByteArray();
}

FigmaQml::FigmaQml(const QString& qmlDir, const QString& fontFolder, const ImageProvider& byteProvider, const NodeProvider& dataProvider, QObject *parent) : QObject(parent),
    m_qmlDir(qmlDir), mImageProvider(byteProvider), mNodeProvider(dataProvider), m_imports(defaultImports()), m_fontCache(std::make_unique<FontCache>()), m_fontFolder(fontFolder) {
    qmlRegisterUncreatableType<FigmaQml>("FigmaQml", 1, 0, "FigmaQml", "");
    QObject::connect(this, &FigmaQml::currentElementChanged, this, [this]() {
        m_sourceDoc->getCurrent()->setCurrent(m_uiDoc->getCurrent()->currentIndex());
    });
    QObject::connect(this, &FigmaQml::currentCanvasChanged, this, [this]() {
        m_sourceDoc->setCurrent(m_uiDoc->currentIndex());
    });
    QObject::connect(this, &FigmaQml::sourceCodeChanged, this, &FigmaQml::elementChanged);
    QObject::connect(this, &FigmaQml::currentElementChanged, this, &FigmaQml::sourceCodeChanged);
    QObject::connect(this, &FigmaQml::currentCanvasChanged, this, &FigmaQml::sourceCodeChanged);
    QObject::connect(this, &FigmaQml::currentElementChanged, this, &FigmaQml::elementNameChanged);
    QObject::connect(this, &FigmaQml::currentElementChanged, this, &FigmaQml::componentsChanged);
    QObject::connect(this, &FigmaQml::currentCanvasChanged, this, &FigmaQml::canvasNameChanged);
    QObject::connect(this, &FigmaQml::imageDimensionMaxChanged, this, [this]() {
        if(m_imageDimensionMax <= 0) {
            m_imageDimensionMax = 1024;
        }
    });

    const auto fontFolderChanged = [this]() {
        const QDir fontFolder(m_fontFolder);
        if(!fontFolder.exists())
            emit warning(QString("Folder \"%1\", not found").arg(m_fontFolder));
        for(const auto& entry : fontFolder.entryInfoList()) {
            emit info(entry.fileName());
            if(!entry.fileName().endsWith(".txt")&& QFontDatabase::addApplicationFont(entry.absoluteFilePath()) < 0)
                emit warning(QString("Font \"%1\", cannot be loaded").arg(entry.absoluteFilePath()));
        }
    };

    QObject::connect(this, &FigmaQml::fontFolderChanged, this, fontFolderChanged);
    fontFolderChanged();
}


QVariantMap FigmaQml::defaultImports() {
    return {
#ifdef QT5
        {"QtQuick", QString("2.15")},
        {"QtGraphicalEffects", QString("1.15")},
        {"QtQuick.Shapes", QString("1.15")}
#else
        {"QtQuick", QString("")},
        {"Qt5Compat.GraphicalEffects", QString("")},
        {"QtQuick.Shapes", QString("")}
#endif
    };
}

bool FigmaQml::setBrokenPlaceholder(const QString &placeholder) {
    QFile file(placeholder);
    if(!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    m_brokenPlaceholder = "data:image/jpeg;base64," + file.readAll().toBase64();
    return true;
}


bool FigmaQml::isValid() const {
    return m_uiDoc && !m_uiDoc->empty();
}

QStringList FigmaQml::components() const {
    if(m_sourceDoc && !m_sourceDoc->empty()) {
        const auto currentElement =  m_sourceDoc->current().currentIndex();
        const auto key = m_sourceDoc->current().name(currentElement);
        return m_sourceDoc->components(key);
    } else {
        return QStringList();
    }
}

QByteArray FigmaQml::componentSourceCode(const QString &name) const {
    return (!name.isEmpty()) && m_sourceDoc && m_sourceDoc->containsComponent(name) ? m_sourceDoc->component(name) : QByteArray();
}

QString FigmaQml::componentData(const QString &name) const {
    return (!name.isEmpty()) && m_sourceDoc && m_sourceDoc->containsComponent(name) ? m_sourceDoc->componentData(name) : QString();
}

void FigmaQml::cancel() {
    emit cancelled();
}

void FigmaQml::setFilter(const QMap<int, QSet<int>>& filter) {
    m_filter = filter;
}

QByteArray FigmaQml::prettyData(const QByteArray& data) const {
    QJsonParseError error;
    const auto json = QJsonDocument::fromJson(data, &error);
    if(error.error != QJsonParseError::NoError) {
       return QString("JSON parse error: %1 at %2\n\n")
                .arg(error.errorString())
                .arg(error.offset).toLatin1() + data;
    }
    const auto bytes = json.toJson();
    return bytes;
}

bool FigmaQml::saveImages(const QString &folder) const {
    if(!ensureDirExists(folder))
        return false;
    for(const auto& i : m_imageFiles) {
        const QFileInfo file(i.first + i.second);
        if(!file.exists()) {
            emit error(QString("Invalid filename %1").arg(file.absoluteFilePath()));
            return false;
        }
        const auto target = folder + file.fileName();
        if(!QFile::copy(file.absoluteFilePath(), target)) {
            emit error(QString("Cannot copy %1 to %2").arg(file.absoluteFilePath()).arg(target));
            return false;
        }
    }
    return true;
}

bool FigmaQml::addImageFile(const QString& imageRef, bool isRendering, const QString& targetDir) {
    const auto& [bytes, mime] = mImageProvider(imageRef, isRendering, QSize(m_imageDimensionMax, m_imageDimensionMax));
    if(bytes.isEmpty())
        return false;
    const auto path = targetDir + Images.mid(1);
    int count = 1;
    const QRegularExpression re(R"([\\\/:*?"<>|\s;])");
    auto name = imageRef;
    name.replace(re, QLatin1String("_"));
    Q_ASSERT(mime == PNG || mime == JPEG);
    const QString extension = mime == JPEG ? "jpg" : "png";
    auto imageName = QString("%1.%2").arg(name, extension);
    while(QFile::exists(imageName)) {
        imageName = QString("%1_%2.%3").arg(name).arg(count).arg(extension);
        ++count;
        }
    ensureDirExists(path);
    const auto filename = path + imageName;
    QFile file(filename);
    if(!file.open(QIODevice::WriteOnly)) {
        emit warning("error when write:" + imageRef + " " +  filename + " " + file.errorString());
        return false;
    }
    file.write(bytes);
    m_imageFiles.insert(imageRef, {path, imageName});
    return true;
}

bool FigmaQml::ensureDirExists(const QString& e) const {
    QDir dir(e);
    if(!dir.exists() && !dir.mkpath(".")) {
        emit error(QString("Cannot use dir %1").arg(dir.absolutePath()));
        return false;
    }
    return true;
}

QVariantMap FigmaQml::fonts() const {
    QVariantMap map;
    const auto c = m_fontCache->content();
    for(const auto& v : c)
        map.insert(v.first, v.second);
    return map;
}

void FigmaQml::setFonts(const QVariantMap& map) {
    for(const auto& k : map.keys()) {
       m_fontCache->insert(k, map[k].toString());
    }
}

void FigmaQml::createDocumentView(const QByteArray &data, bool restoreView) {
    const auto json = object(data);
    if(!json)
        return;
    const auto restoredCanvas = currentCanvas();
    const auto restoredElement = currentElement();
    cleanDir(m_qmlDir);
    m_imageFiles.clear();
    m_uiDoc.reset();
    if(!restoreView)
        m_fontCache->clear();
    emit isValidChanged();
    emit canvasCountChanged();
    emit elementCountChanged();
    emit documentNameChanged();
    auto doc = construct<FigmaFileDocument>(*json, m_qmlDir + qmlViewPath, true);
    if(doc) {
        m_uiDoc.swap(doc);
        emit isValidChanged();
        emit canvasCountChanged();
        emit elementCountChanged();
        emit documentNameChanged();
        emit elementChanged();
        createDocumentSources(data);
        emit fontsChanged();
    } else {
        emit error("Invalid document");
    }
    if(restoreView) {
        if(setCurrentCanvas(restoredCanvas))
            setCurrentElement(restoredElement);
    }
}


void FigmaQml::setFontMapping(const QString& key, const QString& value) {
    m_fontCache->insert(key, value);
    emit refresh();
    emit fontsChanged();
}

void FigmaQml::resetFontMappings() {
    m_fontCache->clear();
    emit refresh();
    emit fontsChanged();
}


void FigmaQml::createDocumentSources(const QByteArray &data) {
    const auto json = object(data);
    if(!json)
        return;
    auto doc = construct<FigmaDataDocument>(*json, m_qmlDir + sourceViewPath, m_flags & EmbedImages);
    if(doc) {
        m_sourceDoc.swap(doc);
        emit sourceCodeChanged();
        emit documentCreated();
    } else {
        emit error("Invalid document");
    }
}

void FigmaQml::restore(int flags, const QVariantMap& imports) {
    m_flags = flags;
    m_imports = imports;
}

void FigmaQml::cleanDir(const QString& dirName) const {
    QDir dir(dirName);
    const auto entries = dir.entryInfoList();
    for(const auto& e : entries) {
        if(e.isFile() && !dir.remove(e.fileName()))
            emit error(toStr("Cannot remove", e.fileName()));
    }
}

std::optional<QJsonObject> FigmaQml::object(const QByteArray &data) const {
    if(data.isEmpty())
        return std::nullopt;

    QJsonParseError parseError;
    const auto json = QJsonDocument::fromJson(data, &parseError);
    if(parseError.error != QJsonParseError::NoError) {
       emit this->error(QString("When reading JSON: %1 at %2")
                .arg(parseError.errorString())
                .arg(parseError.offset));
        return std::nullopt;
    }

    if(!json.isObject()) {
        emit error("Object expected");
        return std::nullopt;
    }
    return json.object();
}

bool FigmaQml::busy() const {
    return m_busy;
}

QString FigmaQml::nearestFontFamily(const QString& requestedFont, bool useAlt) {
    if(!useAlt) {
        const QFont font(requestedFont);
        const QFontInfo fontInfo(font);  //this return mapped family
        const auto value = fontInfo.family();
        return value;
    } else {
#ifdef QT5
        QFontDatabase fdb;
        const QStringList fontFamilies = fdb.families();
#else
        const QStringList fontFamilies = QFontDatabase::families();
#endif
        int min = std::numeric_limits<int>::max();
        int index = -1;
        for(auto ff = 0; ff < fontFamilies.size() ; ff++) {
            const auto distance = levenshteinDistance(fontFamilies[ff], requestedFont);
            if(distance < min) {
                index = ff;
                min = distance;
            }
        }
        if(index < 0) {
            return requestedFont;
        }
        const auto value = fontFamilies[index];
        return value;
    }
}

void FigmaQml::setSignals(bool allow) {
    blockSignals(!allow);
}

template<class T>
std::unique_ptr<T> FigmaQml::construct(const QJsonObject& obj, const QString& targetDir, bool embedImages) const {
    std::atomic_bool doCancel = false;
    const auto d = QObject::connect(this, &FigmaQml::cancelled, this, [&doCancel]() {
        doCancel = true;
    }, Qt::UniqueConnection);
    RAII(([d](){QObject::disconnect(d);}));

    m_busy = true;
    emit busyChanged();
    RAII([this]() {m_busy = false; emit busyChanged();});

    auto doc = std::make_unique<T>(targetDir, FigmaParser::name(obj));

    std::atomic_bool ok = true;
    const auto errorFunction = [this, &ok, &doCancel](const QString& str, bool isFatal) {
        if(!doCancel) {
            if(isFatal) {
                ok = false;
                emit error(str);
            } else
                emit warning(str);
        }
    };

    Q_ASSERT(m_imageDimensionMax > 0);

    const auto fontFunction = [this](const QString& requestedFont) {
        if(m_flags & KeepFigmaFontName)
            return requestedFont;

        if(m_fontCache->contains(requestedFont))
            return (*m_fontCache)[requestedFont];

        const auto value = nearestFontFamily(requestedFont, m_flags & AltFontMatch);
        m_fontCache->insert(requestedFont, value);
        return value;
    };

    const auto imageFunction = [this, &doCancel, &ok, &targetDir, embedImages](const QString& imageRef, bool isRendering) {
        if(!ok || doCancel)
            return QByteArray();
        if(imageRef == FigmaParser::PlaceHolder)
            return m_brokenPlaceholder;
        else {
            if(embedImages) {
                const auto& [bytes, mime] = mImageProvider(imageRef, isRendering, QSize(m_imageDimensionMax, m_imageDimensionMax));
                if(bytes.isEmpty())
                    return QByteArray();
                Q_ASSERT(mime == JPEG || mime == PNG);
                const QByteArray mimeString = mime == JPEG ? "jpeg" : "png";
                return "data:image/" + mimeString + ";base64," + bytes.toBase64();
            } else {
                if(!m_imageFiles.contains(imageRef) && !const_cast<FigmaQml*>(this)->addImageFile(imageRef, isRendering, targetDir)) //cache a like stuff is ok to const casted
                    return QByteArray();
                return (Images.mid(1) +  m_imageFiles[imageRef].second).toLatin1();
                }
            }

        };

   if(!ensureDirExists(targetDir))
       return nullptr;

    QByteArray header = QString(FileHeader).toLatin1();
    for(const auto& k : m_imports.keys()) {
#ifdef QT5
        const auto ver = QVersionNumber::fromString(m_imports[k].toString());
        if(ver.isNull()) {
            emit error(toStr("Invalid imports version", m_imports[k].toString(), "for", k));
            return nullptr;
        }
        header += QString("import %1 %2\n").arg(k).arg(m_imports[k].toString());
#else
        header += QString("import %1\n").arg(k);
#endif
    }

    auto components = FigmaParser::components(obj, errorFunction, [this, &doCancel, &ok](const QString& id) {
        if(!ok || doCancel)
            return QByteArray();
        return mNodeProvider(id);
    });

    TIMED_START(t3)

#ifdef NON_CONCURRENT
    for(const auto& c : components) {
      const auto component = FigmaParser::component(c->object(), m_flags, errorFunction, imageFunction, fontFunction, components);
      if(!ok || doCancel)
          return nullptr;

      if(component.data().isEmpty()) {
          emit error(toStr("Invalid component", component.name()));
          return nullptr;
      }

      doc->addComponent(components[component.id()]->name(), components[component.id()]->object(), header + component.data());

      QStringList componentNames;
      for(const auto& id : component.components()) {
          Q_ASSERT(components.contains(id)); //just check here
          const auto compname = components[id]->name();
          componentNames.append(compname);
      }

      QFile componentFile(targetDir + "/" + validFileName(c->name()) + ".qml");
      if(componentFile.exists()) {
          emit error(toStr("File aleady exists", componentFile.fileName(), QString("\"%1\" \"%2\"").arg(c->name()).arg(c->description())));
          return nullptr;
      }
      if(!componentFile.open(QIODevice::WriteOnly)) {
          emit error(toStr("Cannot write", componentFile.fileName(), componentFile.errorString()));
          return nullptr;
      }
      componentFile.write(header + component.data());
    }
#else

    const auto componentData =
            Concurrent::mapped<FigmaParser::Components::iterator, std::function<FigmaParser::Element (const FigmaParser::Components::const_iterator::value_type &) >>
                          (components.begin(), components.end(), [&, this](const auto& c)->FigmaParser::Element {

            if(!ok || doCancel)
                return FigmaParser::Element();

             const auto component = FigmaParser::component(c->object(), m_flags, errorFunction, imageFunction, fontFunction, components);

            if(component.data().isEmpty()) {
                ok = false;
                emit error(toStr("Invalid component", component.name()));
                return FigmaParser::Element();
            }

            QFile componentFile(targetDir + c->name() + ".qml");
#ifdef _DEBUG
            if(componentFile.exists()) {
                emit info(toStr("File updated", componentFile.fileName(), QString("\"%1\" \"%2\"").arg(c->name()).arg(c->description())));
            }
#endif
            if(!componentFile.open(QIODevice::WriteOnly)) {
                ok = false;
                emit error(toStr("Cannot write", componentFile.fileName(), componentFile.errorString()));
                return FigmaParser::Element();
            }
            componentFile.write(header + component.data());

            return component;
        });

    FutureWatcher<FigmaParser::Element> watch;
    QEventLoop loop;
    QObject::connect(&watch, &FutureWatcher<FigmaParser::Element>::finished, &loop, &QEventLoop::quit);
    QObject::connect(this, &FigmaQml::error, [&doCancel](){
        doCancel = true;
    });
    watch.setFuture(componentData);
    loop.exec();

    if(!watch.isFinished()) {
        watch.waitForFinished();
        return nullptr;
    }

    if(!ok || doCancel)
        return nullptr;

    const std::function<void (const FigmaParser::Element& c)> addComponent = [&](const FigmaParser::Element& c) { //recursive lambdas cannot be declared auto
        if(!ok || doCancel)
            return;
        const auto name = components[c.id()]->name();
        if(doc->containsComponent(name))
            return;
        QStringList componentNames;
        doc->addComponent(name, components[c.id()]->object(), header + c.data());
        for(const auto& id : c.components()) {
            if(!ok || doCancel)
                return;
            Q_ASSERT(components.contains(id)); //just check here
            const auto compname = components[id]->name();
            componentNames.append(compname);
            const auto cit = std::find_if(componentData.begin(), componentData.end(), [&id](const auto& c){return c.id() == id;});
            addComponent(*cit);
        }
        doc->setComponents(name, componentNames);
    };

    for(const auto& c : componentData) {
        addComponent(c);
    }

#endif

    TIMED_END(t3, "Component")
    TIMED_START(t4)

    int currentCanvas = 0;
#ifdef NON_CONCURRENT
    int currentElement = 0;
#else
    std::atomic_int currentElement = 0;
#endif
    const auto canvases = FigmaParser::canvases(obj, errorFunction);
    for(const auto& c : canvases) {
        ++currentCanvas;
        currentElement = 0;
        auto canvas = doc->addCanvas(c.name());
#ifdef NON_CONCURRENT
        for(const auto& f : c.elements()) {
            if(doCancel)
                return nullptr;
            bool hasElement = true;
            if(!m_filter.isEmpty()) {
                ++currentElement;
                if(!m_filter.keys().contains(currentCanvas) || !m_filter[currentCanvas].contains(currentElement))
                    hasElement = false;
            }
            const auto element = hasElement ? FigmaParser::element(f, m_flags, errorFunction, imageFunction, fontFunction, components) : FigmaParser::Element();
#else
        auto elements = c.elements();
        Future<FigmaParser::Element> elementData =
        Concurrent::mapped<FigmaParser::Canvas::ElementVector::iterator, std::function<FigmaParser::Element (const FigmaParser::Canvas::ElementVector::value_type&)> > (
        elements.begin(), elements.end(), [&, this](const auto& f) {
            if(!ok || doCancel)
                return FigmaParser::Element();
            if(!m_filter.isEmpty()) {
                const int ce = currentElement.fetch_add(1);
                if(!m_filter.contains(currentCanvas) || !m_filter[currentCanvas].contains(ce))
                    return FigmaParser::Element();
            }
            return FigmaParser::element(f, m_flags, errorFunction, imageFunction, fontFunction, components);

        });

        FutureWatcher<FigmaParser::Element> watch;
        QEventLoop loop;
        QObject::connect(&watch, &FutureWatcher<FigmaParser::Element>::finished, &loop, &QEventLoop::quit);
        QObject::connect(this, &FigmaQml::error, [&doCancel](){
            doCancel = true;
        });
        watch.setFuture(elementData);
        loop.exec();

        if(!watch.isFinished()) {
            watch.waitForFinished();
            return nullptr;
        }

        if(!ok || doCancel)
            return nullptr;

        for(const auto& element : elementData) {
#endif
            if(doCancel)
                return nullptr;
            if(!ok) {
                return nullptr;
            }
            if(!element.data().isEmpty())
                canvas->addElement(element.name(), header + element.data());
            else
                canvas->addElement(element.name(), header + "Text{text: \"filtered out\"}");
            QStringList componentNames;
            for(const auto& id : element.components()) {
                componentNames.append(components[id]->name());
            }
            doc->setComponents(element.name(), std::move(componentNames));
        }
    }

    TIMED_END(t4, "elements")
    return doc;
}

#ifdef WASM_FILEDIALOGS
QString FigmaQml::saveAllQMLZipped(const QString& docName, const QString& canvasName) {
    QTemporaryDir temp;
    if(!saveAllQML(temp.path()))
        return QString{}; // an error
  //  QFileDialog::saveFileContent(const QByteArray &fileContent, const QString &fileNameHint = QString())
}

bool FigmaQml::importFontFolder() {
    //QFileDialog::getOpenFileContent(const QString &nameFilter, const std::function<void (const QString &, const QByteArray &)> &fileOpenCompleted)
    return false;
}

QString FigmaQml::store(const QString& docName) {
  //  void QFileDialog::saveFileContent(const QByteArray &fileContent, const QString &fileNameHint = QString())
    return QString{};
}

QString FigmaQml::restore() {
 //   QFileDialog::getOpenFileContent(const QString &nameFilter, const std::function<void (const QString &, const QByteArray &)> &fileOpenCompleted)
    return QString{};
}

#endif
