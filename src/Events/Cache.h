#ifndef LOCAL_CACHE_H
#define LOCAL_CACHE_H

#include <QtCore/QObject>
#include <QtCore/QMutex>

#include "APIClient/ApiTypes.h"

namespace Drive
{

struct RemoteFileEvent;
class RemoteFileEventExclusion;
struct LocalFileEvent;
class LocalFileEventExclusion;

class LocalCache : public QObject
{
    Q_OBJECT
public:

    static LocalCache& instance();

    LocalCache(QObject *parent = 0);

    int id(const QString& remotePath, bool forParent = false);
    RemoteFileDesc fileDesc(const QString& remotePath, bool forParent = false);

    void clear();
    void addDiskItem();

    void log(const QString& fileName = QString());

public slots:
    void onNewFileDesc(Drive::RemoteFileDesc fileDesc);
    //void onRemoveFileDesc()

private:
    QMap<QString, RemoteFileDesc> pathMap;
    QMap<int, RemoteFileDesc> idMap;

    int diskId;
    
    QMutex mutex;
};

}

#endif // LOCAL_CACHE_H