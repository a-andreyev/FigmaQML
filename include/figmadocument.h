#ifndef FIGMADOCUMENT_H
#define FIGMADOCUMENT_H

#include <QHash>
#include <QString>
#include <QByteArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <vector>

class FigmaDocument {
public:
    virtual ~FigmaDocument() = default;
    class Canvas {
    public:
        class Element {
        public:
            virtual ~Element() {}
            virtual QByteArray data() const = 0;
            virtual QString name() const = 0;
        };
        using ElementVector = std::vector<std::unique_ptr<Element>>;
    public:
        explicit Canvas(const QString& canvasName) : m_name(canvasName) {
        }
        virtual ~Canvas(){}
        int currentIndex() const {
            return m_current;
        }

        void setCurrent(int index) {
            m_current = index;
        }

        QString name() const {
            return m_name;
        }

        virtual bool addElement(const QString& name, const QByteArray& data) = 0;

        int size() const {
            return static_cast<int>(m_elements.size());
        }
        ElementVector::const_iterator begin() const {
            return m_elements.begin();
        }
        ElementVector::const_iterator end() const {
            return m_elements.end();
        }

        QByteArray data() const {
            return m_elements[m_current]->data();
        }

        bool empty() const {
            return m_elements.empty();
        }

        QString name(int index) const {
            return index >= 0 && static_cast<int>(m_elements.size()) > index ? m_elements[index]->name() : QString();
        }

    protected:
        const QString m_name;
        int m_current = 0;
        ElementVector m_elements;
    };
    using CanvasVector = std::vector<std::unique_ptr<Canvas>>;
public:
    FigmaDocument(const QString& name) : m_name(name){}

    int size() const {
        return static_cast<int>(m_canvas.size());
    }

    const Canvas& current() const {
        return *m_canvas[m_current];
    }

    bool empty() const {
        return m_canvas.empty() || current().empty();
    }

    int currentIndex() const {
        return m_current;
    }


    QString name() const {
        return m_name;
    }

    Canvas* getCurrent() {
        return m_canvas[m_current].get();
    }

    void setCurrent(int index) {
        m_current = index;
    }

    CanvasVector::const_iterator begin() const {
        return m_canvas.begin();
    }
    CanvasVector::const_iterator end() const {
        return m_canvas.end();
    }

    void setComponents(const QString& name, const QStringList& components) {
        Q_ASSERT(!name.isEmpty());
        if(m_componentMap.contains(name))
            m_componentMap[name].unite(QSet<QString>(components.begin(), components.end()));
        else
            m_componentMap.insert(name, QSet<QString>(components.begin(), components.end()));
    }


    virtual Canvas* addCanvas(const QString& canvasName) = 0;
    virtual bool containsComponent(const QString& name) const = 0;
    virtual void addComponent(const QString& name, const QJsonObject& obj, const QByteArray& data) = 0;

protected:
    const QString m_name;
    int m_current = 0;
    CanvasVector m_canvas;
    QHash<QString, QSet<QString>> m_componentMap;
};

enum class DocumentType {
    FileDocument, DataDocument
};

class FigmaFileDocument : public FigmaDocument {
    class CanvasFile : public FigmaDocument::Canvas {
        class ElementFile : public FigmaDocument::Canvas::Element {
        public:
            ElementFile(const QString& name, const QString& directory) : m_name(name), m_data((directory + name + ".qml").toLatin1()) {
            }
            bool bless(const QByteArray& data) {
               QFile f(m_data);
                if(f.exists()) {
                   qDebug() << "Not replace" << m_name;
                   return true;
                }
               if(!f.open(QIODevice::WriteOnly))
                   return false;
               f.write(data);
               return true;
            }
            QByteArray data() const override {return m_data;}
            QString name() const override {return m_name;}
        private:
            const QString m_name;
            const QByteArray m_data;
        };
    public:
        explicit CanvasFile(const QString& name, const QString* directory) : Canvas(name), m_directory(directory) {}
        bool addElement(const QString& name, const QByteArray& data) override {
             Q_ASSERT(!name.isEmpty());
             Q_ASSERT(!data.isEmpty());
             m_elements.push_back(std::make_unique<ElementFile>(name, *m_directory));
             if(!reinterpret_cast<ElementFile*>(m_elements.back().get())->bless(data))
                 return false;
             return true;
         }
    private:
        const QString* m_directory;
    };
public:
     static DocumentType type() {return DocumentType::FileDocument;}
     FigmaFileDocument(const QString& directory, const QString& name) : FigmaDocument(name), m_directory(directory) {
     }

     ~FigmaFileDocument() {
         for(const auto& c : *this) {
             for(const auto& e : *c) {
                 QFile::remove(e->data());
             }
         }
     }

     bool containsComponent(const QString& name) const override {
        return m_components.contains(name);
     }

     void addComponent(const QString& name, const QJsonObject& obj, const QByteArray& data) override {
         Q_UNUSED(obj);
         Q_UNUSED(data);
         m_components.insert(name);
     }

     Canvas* addCanvas(const QString& canvasName) override  {
         m_canvas.push_back(std::make_unique<CanvasFile>(canvasName, &m_directory));
         return m_canvas.back().get();
     }
private:
    const QString m_directory;
    QSet<QString> m_components;
};

class FigmaDataDocument : public FigmaDocument {

    class CanvasData : public FigmaDocument::Canvas {
        class ElementData : public FigmaDocument::Canvas::Element {
        public:
            ElementData(const QString& name, const QByteArray& data) : m_name(name), m_data(data) {}
            QByteArray data() const override {return m_data;}
            QString name() const override {return m_name;}
        private:
            const QString m_name;
            const QByteArray m_data;
        };
    public:
        explicit CanvasData(const QString& name) : Canvas(name) {}
        bool addElement(const QString& name, const QByteArray& data) override {
             Q_ASSERT(!name.isEmpty());
             Q_ASSERT(!data.isEmpty());
             m_elements.push_back(std::make_unique<ElementData>(name, data));
             return true;
         }
    };
public:
    static DocumentType type() {return DocumentType::DataDocument;}
    FigmaDataDocument(const QString& directory, const QString& name) : FigmaDocument(name) {
        Q_UNUSED(directory);
    }

    QStringList components(const QString& elementName) const {
        QSet<QString> allComponents;
        getComponents(allComponents, elementName);
        QStringList lst = allComponents.values();
        lst.sort();
        return lst;
    }

    Canvas* addCanvas(const QString& canvasName) override {
        m_canvas.push_back(std::make_unique<CanvasData>(canvasName));
        return m_canvas.back().get();
    }

    bool containsComponent(const QString& name) const override {
        return m_components.contains(name);
    }

    static int remove_comments(const QByteArray& data, unsigned& end) {
        if(data[end] != '/' || end + 1 == data.size())
            return 0;
        if(data[end + 1] == '/') {
            ++end;
            while(end < data.size() && data[++end] != '\n');
            return 1;
        } else if(data[end + 1] == '*') {
            ++end;
            int lines = 0;
            while(end < data.size()) {
                const auto c = data[end++];
                if(c == '\n')
                    ++lines;
                if(c == '*' && end < data.size() && data[end] == '/') {
                    ++end;
                    return lines;
                }
            }
        } else {
            return 0;
        }
        return 0;
    }

    void addComponent(const QString& name, const QJsonObject& obj, const QByteArray& data) override {
        Q_ASSERT(!name.isEmpty());
        Q_ASSERT(!data.isEmpty());
        if(m_components.contains(name)) {
            if(qChecksum(data) == qChecksum(m_components[name].first)) {
                return;
            }
            unsigned rbegin = 0;
            unsigned cbegin = 0;
            unsigned cend = 0;
            unsigned rend = 0;
            const auto& ref = m_components[name].first;
            auto cline = 1;
            auto rline = 1;
            while(cend < data.size() && rend < ref.size()) {
                const auto c = data[cend];
                const auto r = ref[rend];
                cline += remove_comments(data, cend);
                rline += remove_comments(ref, rend);
                if(c == '\n' && r != '\n')
                    ++rend;
                else if(c != '\n' && r == '\n')
                    ++cend;
                else if (c == '\n' || r == '\n') {
                    auto c_slice= data.mid(cbegin, cend).simplified();
                    auto r_slice= ref.mid(rbegin, rend).simplified();
                    if(c_slice != r_slice) {
                        qDebug() << "warn:" << name << "mismatch component" << cline << rline << data.mid(cbegin, cend) << "\n\n" << ref.mid(rbegin, rend);
                        break;
                    }
                    ++rend;
                    ++cend;
                    cbegin = cend;
                    rbegin = rend;
                    ++rline;
                    ++cline;
                } else {
                    ++rend;
                    ++cend;
                }
            }
            if(cend == data.size() || rend == ref.size())
                qDebug() << "warn:" << name << "were close component";
        }
        m_components.insert(name, {data, QJsonDocument(obj).toJson()});
    }

    QByteArray component(const QString& componentName) const {
        Q_ASSERT(m_components.contains(componentName));
        return m_components[componentName].first;
    }

    QByteArray componentObject(const QString& componentName) const {
        Q_ASSERT(m_components.contains(componentName));
        return m_components[componentName].second;
    }

private:
    void getComponents(QSet<QString>& componentSet, const QString& elementName) const {
        for(const QString& component : m_componentMap[elementName]) {
            if(!componentSet.contains(component)){
                componentSet.insert(component);
                getComponents(componentSet, component);
            }
        }
    }
private:
    QHash<QString, QPair<QByteArray, QByteArray>> m_components;
};


#endif // FIGMADOCUMENT_H
