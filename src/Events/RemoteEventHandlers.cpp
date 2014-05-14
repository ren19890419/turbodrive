#include "RemoteEventHandlers.h"

#include "Cache.h"

#include "Events/LocalFileEvent.h"
#include "QsLog/QsLog.h"
#include "APIClient/FilesService.h"
#include "APIClient/FileDownloader.h"
#include "Util/FileUtils.h"

#include <QtCore/QDir>

namespace Drive
{

RemoteEventHandlerBase::RemoteEventHandlerBase(
		RemoteFileEvent remoteEvent, QObject *parent)
	: EventHandlerBase(parent)
	, m_remoteEvent(remoteEvent)
{
}

// ===========================================================================

RemoteFolderCreatedEventHandler::RemoteFolderCreatedEventHandler(
		RemoteFileEvent remoteEvent, QObject *parent)
	: RemoteEventHandlerBase(remoteEvent, parent)
{
}

void RemoteFolderCreatedEventHandler::run()
{
	if (!m_remoteEvent.isValid())
	{
		QLOG_ERROR() << "remote event is not valid";
		return;
	}

	if (m_remoteEvent.type != RemoteFileEvent::Created)
	{
		QLOG_ERROR() << "remote event type:" << m_remoteEvent.type
			<< ", should be" << RemoteFileEvent::Created;
		return;
	}

	if (m_remoteEvent.fileDesc.type == RemoteFileDesc::File)
	{
		QLOG_ERROR() << "Remote event 'created' contains a file, not a folder";
		return;
	}

	GetAncestorsRestResourceRef getAncestorsRes =
		GetAncestorsRestResource::create();

	connect(getAncestorsRes.data(), &GetAncestorsRestResource::succeeded,
		this, &RemoteFolderCreatedEventHandler::onGetAncestorsSucceeded);

	getAncestorsRes->getAncestors(m_remoteEvent.fileDesc.id);

	exec();
}

void RemoteFolderCreatedEventHandler::onGetAncestorsSucceeded(const QString& fullPath)
{
	const QString localFolder = Utils::toLocalPath(fullPath);

	QDir dir(localFolder);
	if (!dir.exists())
	{
		LocalFileEventExclusion exclusion(LocalFileEvent::Added, localFolder);

		emit newLocalFileEventExclusion(exclusion);

		if (dir.mkpath("."))
			QLOG_INFO() << "Local folder created:" << localFolder;
		else
		{
			QString errorMsg =
				QString("Local folder creation failed: %1").arg(localFolder);

			emit failed(errorMsg);
			QLOG_ERROR() << errorMsg;
		}
	}
	else
	{
		QLOG_INFO() << "Local folder already exists:" << localFolder;
	}

	processEventsAndQuit();
}

void RemoteFolderCreatedEventHandler::onGetAncestorsFailed()
{
	emit failed("Failed to get the remote folder path");
	processEventsAndQuit();
}

// ===========================================================================

RemoteFileRenamedEventHandler::RemoteFileRenamedEventHandler(
	RemoteFileEvent remoteEvent, QObject *parent)
	: RemoteEventHandlerBase(remoteEvent, parent)
{
}

void RemoteFileRenamedEventHandler::run()
{
	if (!m_remoteEvent.isValid())
	{
		return;
	}

	if (m_remoteEvent.type != RemoteFileEvent::Renamed
		&& m_remoteEvent.type != RemoteFileEvent::Moved)
	{
		return;
	}

	GetAncestorsRestResourceRef getAncestorsRes =
		GetAncestorsRestResource::create();

	connect(getAncestorsRes.data(), &GetAncestorsRestResource::succeeded,
			this, &RemoteFileRenamedEventHandler::onGetAncestorsSucceeded);

	connect(getAncestorsRes.data(), &GetAncestorsRestResource::failed,
			this, &RemoteFileRenamedEventHandler::onGetAncestorsFailed);

	getAncestorsRes->getAncestors(m_remoteEvent.fileDesc.id);

	exec();
}

void RemoteFileRenamedEventHandler::onGetAncestorsSucceeded(const QString& fullPath)
{
	const QString newLocalPath = Utils::toLocalPath(fullPath);

	LocalCache& cache = LocalCache::instance();

	const RemoteFileDesc file = cache.file(m_remoteEvent.fileDesc.id);
	Q_ASSERT(file.isValid());

	const QString oldLocalPath = Utils::toLocalPath(cache.fullPath(file));
	if (oldLocalPath != newLocalPath)
	{
		Q_EMIT newLocalFileEventExclusion(
				LocalFileEventExclusion(LocalFileEvent::Added, newLocalPath));
		Q_EMIT newLocalFileEventExclusion(
				LocalFileEventExclusion(LocalFileEvent::Deleted, oldLocalPath));

		if(!QFile::rename(oldLocalPath, newLocalPath))
		{
			QLOG_DEBUG() << oldLocalPath;
			QLOG_DEBUG() << newLocalPath;
			Q_ASSERT(!"Failed to rename the file.");
		}
	}

	cache.addFile(m_remoteEvent.fileDesc);

	processEventsAndQuit();
}

void RemoteFileRenamedEventHandler::onGetAncestorsFailed()
{
	emit failed("Failed to get the remote folder path");
	processEventsAndQuit();
}

// ===========================================================================

RemoteFileTrashedEventHandler::RemoteFileTrashedEventHandler(
	RemoteFileEvent remoteEvent, QObject *parent)
	: RemoteEventHandlerBase(remoteEvent, parent)
{
}

void RemoteFileTrashedEventHandler::run()
{
	if (!m_remoteEvent.isValid())
		return;

	if (m_remoteEvent.type != RemoteFileEvent::Trashed)
		return;

	onGetAncestorsSucceeded(m_remoteEvent.fileDesc.originalPath);
}

void RemoteFileTrashedEventHandler::onGetAncestorsSucceeded(
	const QString& fullPath)
{
	m_localPath = Utils::toLocalPath(fullPath);
	m_localPath.append(Utils::separator());
	m_localPath.append(m_remoteEvent.fileDesc.name);

	QFileInfo fileInfo(m_localPath);
	if (!fileInfo.exists())
	{
		Q_EMIT quitThread();
	}

	if (fileInfo.isFile() || fileInfo.isSymLink())
	{
		LocalFileEventExclusion exclusion(LocalFileEvent::Deleted, m_localPath);
		emit newLocalFileEventExclusion(exclusion);

		QFile::remove(m_localPath);
	}
	else // dir or bundle
	{
		LocalFileEventExclusion exclusion(LocalFileEvent::Deleted
			, m_localPath
			, LocalFileEventExclusion::PartialMatch);

		emit newLocalFileEventExclusion(exclusion);

		FileSystemHelper::removeDirWithSubdirs(m_localPath);
	}

	Q_EMIT quitThread();
}

void RemoteFileTrashedEventHandler::onGetAncestorsFailed()
{
	emit failed("Failed to get the remote file object path");
	Q_EMIT quitThread();
}

// ===========================================================================

RemoteFileUploadedEventHandler::RemoteFileUploadedEventHandler(
	RemoteFileEvent remoteEvent, QObject *parent)
	: RemoteEventHandlerBase(remoteEvent, parent)
	, m_localFilePath(QString())
{
}

void RemoteFileUploadedEventHandler::run()
{
	QLOG_INFO() << "RemoteFileUploadedEventHandler::run(): "
		<< this;

	m_remoteEvent.logCompact();

	if (!m_remoteEvent.isValid())
	{
		QLOG_ERROR() << "Remote file event is not valid:";
		m_remoteEvent.logCompact();
		return;
	}

	if (m_remoteEvent.type != RemoteFileEvent::Uploaded)
		return;

	if (m_remoteEvent.fileDesc.type == RemoteFileDesc::Dir)
	{
		QLOG_ERROR() <<
			"Remote event 'uploaded' contains a folder, not a file";
		return;
	}

	GetAncestorsRestResourceRef getAncestorsRes =
		GetAncestorsRestResource::create();

	connect(getAncestorsRes.data(), &GetAncestorsRestResource::succeeded,
			this, &RemoteFileUploadedEventHandler::onGetAncestorsSucceeded);

	connect(getAncestorsRes.data(), &GetAncestorsRestResource::failed,
			this, &RemoteFileUploadedEventHandler::onGetAncestorsFailed);

	getAncestorsRes->getAncestors(m_remoteEvent.fileDesc.id);

	exec();
}

void RemoteFileUploadedEventHandler::onGetAncestorsSucceeded(
	const QString& fullPath)
{
	m_localFilePath = Utils::toLocalPath(fullPath);

	QFileInfo fileInfo(m_localFilePath);
	if (fileInfo.exists())
	{
		const uint localModified = fileInfo.lastModified().toTime_t();
		if (localModified > m_remoteEvent.fileDesc.modifiedAt)
		{
			Q_EMIT newLocalFileEvent(LocalFileEvent(LocalFileEvent::Modified,
					QDir::cleanPath(fileInfo.absolutePath()),
					fileInfo.fileName()));
			processEventsAndQuit();
			Q_EMIT succeeded();
			return;
		}
		else if (localModified == m_remoteEvent.fileDesc.modifiedAt)
		{
			Q_EMIT succeeded();
			processEventsAndQuit();
			return;
		}
	}

	m_downloader = new FileDownloader(m_remoteEvent.fileDesc.id,
		m_localFilePath, m_remoteEvent.fileDesc.modifiedAt, this);

	connect(m_downloader, &FileDownloader::succeeded,
			this, &RemoteFileUploadedEventHandler::onDownloadSucceeded);

	connect(m_downloader, &FileDownloader::failed,
			this, &RemoteFileUploadedEventHandler::onDownloadFailed);

	Q_EMIT newLocalFileEventExclusion(LocalFileEventExclusion(
			LocalFileEvent::Added, m_localFilePath));
	Q_EMIT newLocalFileEventExclusion(LocalFileEventExclusion(
			LocalFileEvent::Modified, m_localFilePath));

	m_downloader->limitSpeed(50);
	m_downloader->download();

}

void RemoteFileUploadedEventHandler::onGetAncestorsFailed()
{
	emit failed("Failed to get the remote file path");
	processEventsAndQuit();
}

void RemoteFileUploadedEventHandler::onDownloadSucceeded()
{
	QLOG_TRACE() << "Download succeeded";
	processEventsAndQuit();
}

void RemoteFileUploadedEventHandler::onDownloadFailed(const QString& error)
{
	QLOG_ERROR() << "Download failed";
	emit failed(
		QString(tr("File uploaded event handler failed: %1")).arg(error));

	processEventsAndQuit();
}

// ===========================================================================

RemoteFileOrFolderRestoredEventHandler::RemoteFileOrFolderRestoredEventHandler(
	RemoteFileEvent remoteEvent,
	QObject *parent)
	: RemoteEventHandlerBase(remoteEvent, parent)
{
}

void RemoteFileOrFolderRestoredEventHandler::run()
{
	QLOG_INFO() << "RemoteFileRestoredEventHandler::run(): "
		<< this;

	m_remoteEvent.logCompact();

	if (!m_remoteEvent.isValid())
	{
		QLOG_ERROR() << "Remote file event is not valid:";
		m_remoteEvent.logCompact();
		return;
	}

	if (m_remoteEvent.type != RemoteFileEvent::Restored)
		return;

	if (m_remoteEvent.fileDesc.type == RemoteFileDesc::Dir)
	{
		// 1. fire folder created remote event
		// 2. if has children, then getChildren and create remote events for them

		RemoteFileEvent newRemoteEvent(m_remoteEvent);
		newRemoteEvent.type = RemoteFileEvent::Created;
		emit newPriorityRemoteFileEvent(newRemoteEvent);

		if (m_remoteEvent.fileDesc.hasChildren)
		{
			GetChildrenResourceRef getChildrenRes =
				GetChildrenResource::create();

			connect(getChildrenRes.data(), &GetChildrenResource::succeeded,
					this, &RemoteFileOrFolderRestoredEventHandler::onGetChildrenSucceeded);

			connect(getChildrenRes.data(), &GetChildrenResource::failed,
					this, &RemoteFileOrFolderRestoredEventHandler::onGetChildrenFailed);

			getChildrenRes->getChildren(m_remoteEvent.fileDesc.id);

			exec();
		}
	}
	else if (m_remoteEvent.fileDesc.type == RemoteFileDesc::File)
	{
		RemoteFileEvent newRemoteEvent(m_remoteEvent);
		newRemoteEvent.type = RemoteFileEvent::Uploaded;
		emit newPriorityRemoteFileEvent(newRemoteEvent);

		emit succeeded();
		processEventsAndQuit();
	}
}

void RemoteFileOrFolderRestoredEventHandler::onGetChildrenSucceeded(
	QList<RemoteFileDesc> list)
{
	foreach (RemoteFileDesc fileDesc, list)
	{
		RemoteFileEvent newRemoteEvent;
		newRemoteEvent.fileDesc = fileDesc;
		newRemoteEvent.timestamp = m_remoteEvent.timestamp;
		newRemoteEvent.unixtime = m_remoteEvent.unixtime;
		newRemoteEvent.projectId = m_remoteEvent.projectId;
		newRemoteEvent.workspaceId = m_remoteEvent.workspaceId;

		if (fileDesc.type == RemoteFileDesc::Dir)
		{
			newRemoteEvent.type = RemoteFileEvent::Restored;
		}
		else // i.e. fileDesc.type == RemoteFileDesc::File
		{
			newRemoteEvent.type = RemoteFileEvent::Uploaded;
		}

		emit newPriorityRemoteFileEvent(newRemoteEvent);
	}

	emit succeeded();
	processEventsAndQuit();
}

void RemoteFileOrFolderRestoredEventHandler::onGetChildrenFailed()
{
	QLOG_ERROR() << this << "Failed to get children.";
	emit failed("Failed to get children.");
	processEventsAndQuit();
}

// ===========================================================================

RemoteFileCopiedEventHandler::RemoteFileCopiedEventHandler(
	RemoteFileEvent remoteEvent, QObject *parent)
	: RemoteEventHandlerBase(remoteEvent, parent)
{
}

void RemoteFileCopiedEventHandler::run()
{
	QLOG_INFO() << "RemoteFileCopiedEventHandler::run(): "
		<< this;

	m_remoteEvent.logCompact();

	if (!m_remoteEvent.isValid())
	{
		QLOG_ERROR() << "Remote file event is not valid:";
		m_remoteEvent.logCompact();
		return;
	}

	if (m_remoteEvent.type != RemoteFileEvent::Copied)
		return;

	// 1. get source remote path
	// 2. get target file object
	// 3. get target remote path
	// 4. translate remote paths to local paths
	// 5. do the copy op

	GetAncestorsRestResourceRef getSourceAncestorsRes =
		GetAncestorsRestResource::create();

	connect(getSourceAncestorsRes.data(), &GetAncestorsRestResource::succeeded,
			this, &RemoteFileCopiedEventHandler::onGetAncestorsSucceeded);

	connect(getSourceAncestorsRes.data(), &GetAncestorsRestResource::failed,
			this, &RemoteFileCopiedEventHandler::onGetAncestorsFailed);

	getSourceAncestorsRes->getAncestors(m_remoteEvent.sourceId);

	FilesRestResourceRef filesRestResource = FilesRestResource::create();

//	connect(filesRestResource.data(), &FilesRestResource::getFileObjectSucceeded,
//			this, &RemoteFileCopiedEventHandler::onGetFileObjectSucceeded);

//	connect(filesRestResource.data(), &FilesRestResource::failed,
//			this, &RemoteFileCopiedEventHandler::onGetFileObjectFailed);

	filesRestResource->getFileObject(m_remoteEvent.targetId);

	exec();
}

void RemoteFileCopiedEventHandler::onGetAncestorsSucceeded(QString)
{

}

void RemoteFileCopiedEventHandler::onGetAncestorsFailed()
{

}

}
