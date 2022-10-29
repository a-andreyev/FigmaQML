#ifndef FIGMADATA_H
#define FIGMADATA_H

#include <QString>
#include <QHash>
#include <QDataStream>
#include <QMutex>
#include <tuple>

//TODO: Change to QReadWriteLock - for perf?
#define MUTEX_LOCK(m) QMutexLocker _l(&m);

class FigmaData {
    enum class State {Empty, Pending, Committed};
public:
    bool contains(const QString& key) const {
        MUTEX_LOCK(m_mutex);
        return m_data.contains(key);
    }

    bool isEmpty(const QString& key) const {
        MUTEX_LOCK(m_mutex);
        Q_ASSERT(m_data.contains(key));
        return std::get<State>(m_data[key]) != State::Committed;
    }

    QByteArray data(const QString& key) const {
        MUTEX_LOCK(m_mutex);
        Q_ASSERT(std::get<State>(m_data[key]) == State::Committed);
        Q_ASSERT(m_data.contains(key));
        return std::get<QByteArray>(m_data[key]);
    }

    int format(const QString& key) const {
        MUTEX_LOCK(m_mutex);
        Q_ASSERT(m_data.contains(key));
        return std::get<int>(m_data[key]);
    }

    void insert(const QString& key, const QString& url) {
        MUTEX_LOCK(m_mutex);
        Q_ASSERT(!m_data.contains(key));
        m_data.insert(key, {url, QByteArray(), 0, State::Empty});
    }

    bool isPending(const QString& key) const {
        MUTEX_LOCK(m_mutex);
        Q_ASSERT(m_data.contains(key));
        return std::get<State>(m_data[key]) == State::Pending;
    }

    QString url(const QString& key) const {
         MUTEX_LOCK(m_mutex);
         Q_ASSERT(m_data.contains(key));
         return  std::get<QString>(m_data[key]);
    }
    //Atomic get and set
    bool setPending(const QString& key) {
        MUTEX_LOCK(m_mutex);
        Q_ASSERT(m_data.contains(key));
        const auto wasPending = std::get<State>(m_data[key]) == State::Pending;
        if(wasPending)
            return false;
        Q_ASSERT(std::get<State>(m_data[key]) == State::Empty);
        std::get<State>(m_data[key]) = State::Pending;
        return true;
    }

    void setBytes(const QString& key, const QByteArray& bytes, int meta =  0) {
        Q_ASSERT(std::get<State>(m_data[key]) == State::Pending);
        std::get<QByteArray>(m_data[key]) = bytes;
        std::get<int>(m_data[key]) = meta;
        std::get<State>(m_data[key]) = State::Committed;
    }
    QStringList keys() const {
        MUTEX_LOCK(m_mutex);
        return m_data.keys();
    }

    void clean() {
        MUTEX_LOCK(m_mutex);
        for(auto& e : m_data)
            if(std::get<State>(e) != State::Committed)
                std::get<State>(e) = State::Empty;
    }

    void clear(){
        m_data.clear();
    }

    void write(QDataStream& stream) const {
        const int size = std::accumulate(m_data.begin(), m_data.end(), 0, [](const auto &a, const auto& c){return std::get<State>(c) != State::Committed ? a : a + 1;});
        stream << size;
        const auto keys = m_data.keys();
        for(const auto& key : keys) {
            if(std::get<State>(m_data[key]) == State::Committed) {
                stream
                        << key
                        << std::get<0>(m_data[key])
                        << std::get<1>(m_data[key])
                        << std::get<2>(m_data[key])
                        << std::get<3>(m_data[key]);
            }
        }
    }

    void read(QDataStream& stream) {
        int size;
        stream >> size;
        clear();
        for(int i = 0; i < size; i++) {
            QString key;
            QString d1;
            QByteArray d2;
            int format;
            State state;
            stream >> key;
            stream >> d1;
            stream >> d2;
            stream >> format;
            stream >> state;
            m_data.insert(key, {d1, d2, format, state});
        }
    }
private:
    QHash <QString, std::tuple<QString, QByteArray, int, State> > m_data;
    mutable QMutex m_mutex;
};


#endif // FIGMADATA_H
