// Storage-provider access on Android through ContentResolver (JNI). Calls that can throw use raw
// JNI so the Java exception type is known: QJniObject clears exceptions without reporting them.

#include "DocumentProvider.h"

#ifdef Q_OS_ANDROID

#include <QCoreApplication>
#include <QJniEnvironment>
#include <QJniObject>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace {

struct JavaError
{
    QString className;
    QString message;
    bool any() const { return !className.isEmpty(); }
};

JavaError takeException(JNIEnv *env)
{
    JavaError e;
    jthrowable thrown = env->ExceptionOccurred();
    if (!thrown)
        return e;
    env->ExceptionClear();
    QJniObject throwable(thrown);
    env->DeleteLocalRef(thrown);
    e.className = throwable.callObjectMethod("getClass", "()Ljava/lang/Class;")
                      .callObjectMethod("getName", "()Ljava/lang/String;")
                      .toString();
    e.message = throwable.callObjectMethod("getMessage", "()Ljava/lang/String;").toString();
    if (e.className.isEmpty())
        e.className = QStringLiteral("java.lang.Throwable");
    return e;
}

QString describeJavaError(const JavaError &e)
{
    if (e.className == QLatin1String("java.lang.SecurityException"))
        return QStringLiteral("DrawApp no longer has permission to use this file. Choose the location again with "
                              "Save As (Android: %1).").arg(e.message);
    if (e.className == QLatin1String("java.io.FileNotFoundException"))
        return QStringLiteral("The file is no longer available; it may have been moved or deleted (Android: %1).")
            .arg(e.message);
    return QStringLiteral("%1: %2").arg(e.className, e.message);
}

QString posixError(const char *what, int error)
{
    return QStringLiteral("%1 failed: %2").arg(QLatin1String(what), QString::fromLocal8Bit(std::strerror(error)));
}

class Resolver
{
public:
    explicit Resolver(const QString &uri)
    {
        const QJniObject context(QNativeInterface::QAndroidApplication::context().object());
        m_resolver = context.callObjectMethod("getContentResolver", "()Landroid/content/ContentResolver;");
        m_uri = QJniObject::callStaticObjectMethod("android/net/Uri", "parse", "(Ljava/lang/String;)Landroid/net/Uri;",
                                                   QJniObject::fromString(uri).object<jstring>());
    }

    bool valid() const { return m_resolver.isValid() && m_uri.isValid(); }

    // ParcelFileDescriptor for mode ("r", "rw", "wt"), or an invalid object and *error.
    QJniObject open(const char *mode, JavaError *error)
    {
        JNIEnv *env = m_env.jniEnv();
        jclass cls = env->GetObjectClass(m_resolver.object());
        jmethodID method = env->GetMethodID(cls, "openFileDescriptor",
                                            "(Landroid/net/Uri;Ljava/lang/String;)Landroid/os/ParcelFileDescriptor;");
        env->DeleteLocalRef(cls);
        const QJniObject jmode = QJniObject::fromString(QString::fromLatin1(mode));
        jobject pfd = env->CallObjectMethod(m_resolver.object(), method, m_uri.object(), jmode.object());
        *error = takeException(env);
        if (error->any() || !pfd)
            return {};
        QJniObject result(pfd);
        env->DeleteLocalRef(pfd);
        return result;
    }

    // Closes a ParcelFileDescriptor; providers that stream through a pipe report errors here.
    JavaError close(const QJniObject &pfd)
    {
        JNIEnv *env = m_env.jniEnv();
        jclass cls = env->GetObjectClass(pfd.object());
        jmethodID method = env->GetMethodID(cls, "close", "()V");
        env->DeleteLocalRef(cls);
        env->CallVoidMethod(pfd.object(), method);
        return takeException(env);
    }

    // DocumentsContract flags and OpenableColumns size from a query.
    JavaError query(int *flags, qint64 *size, bool *found)
    {
        JNIEnv *env = m_env.jniEnv();
        jclass stringClass = env->FindClass("java/lang/String");
        jobjectArray projection = env->NewObjectArray(2, stringClass, nullptr);
        env->DeleteLocalRef(stringClass);
        const QJniObject flagsColumn = QJniObject::fromString(QStringLiteral("flags"));
        const QJniObject sizeColumn = QJniObject::fromString(QStringLiteral("_size"));
        env->SetObjectArrayElement(projection, 0, flagsColumn.object());
        env->SetObjectArrayElement(projection, 1, sizeColumn.object());
        jclass cls = env->GetObjectClass(m_resolver.object());
        jmethodID method = env->GetMethodID(cls, "query",
                                            "(Landroid/net/Uri;[Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;"
                                            "Ljava/lang/String;)Landroid/database/Cursor;");
        env->DeleteLocalRef(cls);
        jobject cursorRef = env->CallObjectMethod(m_resolver.object(), method, m_uri.object(), projection, nullptr,
                                                  nullptr, nullptr);
        env->DeleteLocalRef(projection);
        JavaError error = takeException(env);
        *found = false;
        if (error.any() || !cursorRef)
            return error;
        const QJniObject cursor(cursorRef);
        env->DeleteLocalRef(cursorRef);
        if (cursor.callMethod<jboolean>("moveToFirst")) {
            *found = true;
            const jint flagsIndex = cursor.callMethod<jint>("getColumnIndex", "(Ljava/lang/String;)I", flagsColumn.object());
            if (flagsIndex >= 0 && !cursor.callMethod<jboolean>("isNull", "(I)Z", flagsIndex))
                *flags = cursor.callMethod<jint>("getInt", "(I)I", flagsIndex);
            const jint sizeIndex = cursor.callMethod<jint>("getColumnIndex", "(Ljava/lang/String;)I", sizeColumn.object());
            if (sizeIndex >= 0 && !cursor.callMethod<jboolean>("isNull", "(I)Z", sizeIndex))
                *size = cursor.callMethod<jlong>("getLong", "(I)J", sizeIndex);
        }
        cursor.callMethod<void>("close");
        return {};
    }

private:
    QJniEnvironment m_env; // attaches the worker thread to the JVM
    QJniObject m_resolver;
    QJniObject m_uri;
};

int fdOf(const QJniObject &pfd)
{
    return pfd.callMethod<jint>("getFd");
}

class AndroidDocumentProvider : public DocumentProvider
{
public:
    Info probe(const QString &uri) override
    {
        Info info;
        Resolver resolver(uri);
        if (!resolver.valid()) {
            info.error = QStringLiteral("The storage provider could not be reached.");
            return info;
        }
        bool found = false;
        const JavaError queryError = resolver.query(&info.flags, &info.size, &found);
        if (queryError.any()) {
            info.error = describeJavaError(queryError);
            return info;
        }
        JavaError openError;
        const QJniObject pfd = resolver.open("r", &openError);
        if (openError.any() || !pfd.isValid()) {
            info.error = openError.any() ? describeJavaError(openError) : QStringLiteral("The file could not be opened.");
            return info;
        }
        const int fd = fdOf(pfd);
        struct stat st {};
        if (::fstat(fd, &st) == 0) {
            info.regularFile = S_ISREG(st.st_mode);
            if (info.regularFile)
                info.size = qint64(st.st_size);
        }
        struct statvfs vfs {};
        if (info.regularFile && ::fstatvfs(fd, &vfs) == 0)
            info.freeBytes = qint64(vfs.f_bavail) * qint64(vfs.f_frsize);
        resolver.close(pfd);
        return info;
    }

    QString write(const QString &uri, const QByteArray &bytes, bool *touched) override
    {
        *touched = false;
        Resolver resolver(uri);
        if (!resolver.valid())
            return QStringLiteral("The storage provider could not be reached.");
        // "rw" does not truncate, so nothing changes before the space is reserved.
        JavaError error;
        QJniObject pfd = resolver.open("rw", &error);
        if (pfd.isValid()) {
            const int fd = fdOf(pfd);
            struct stat st {};
            if (::fstat(fd, &st) == 0 && S_ISREG(st.st_mode)) {
                const QString result = writeSeekable(fd, qint64(st.st_size), bytes, touched);
                const JavaError closeError = resolver.close(pfd);
                if (!result.isEmpty())
                    return result;
                return closeError.any() ? describeJavaError(closeError) : QString();
            }
            resolver.close(pfd); // a pipe or socket: fall back to a truncating stream below
        } else if (error.className == QLatin1String("java.lang.SecurityException")
                   || error.className == QLatin1String("java.io.FileNotFoundException")) {
            return describeJavaError(error);
        }

        // Stream-only providers (for example cloud storage): "wt" truncates at once.
        pfd = resolver.open("wt", &error);
        if (!pfd.isValid())
            return error.any() ? describeJavaError(error) : QStringLiteral("The file could not be opened for writing.");
        *touched = true;
        const int fd = fdOf(pfd);
        QString result;
        for (qsizetype done = 0; done < bytes.size();) {
            const ssize_t n = ::write(fd, bytes.constData() + done, size_t(bytes.size() - done));
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                result = QStringLiteral("Writing stopped (%1).").arg(posixError("write", errno));
                break;
            }
            done += n;
        }
        const JavaError closeError = resolver.close(pfd);
        if (!result.isEmpty())
            return result;
        return closeError.any() ? QStringLiteral("Writing could not be completed (%1).").arg(describeJavaError(closeError))
                                : QString();
    }

    QByteArray read(const QString &uri, qint64 maxBytes, QString *error) override
    {
        Resolver resolver(uri);
        JavaError javaError;
        const QJniObject pfd = resolver.valid() ? resolver.open("r", &javaError) : QJniObject();
        if (!pfd.isValid()) {
            *error = javaError.any() ? describeJavaError(javaError) : QStringLiteral("The file could not be opened.");
            return {};
        }
        const int fd = fdOf(pfd);
        QByteArray data;
        char buffer[64 * 1024];
        for (;;) {
            const ssize_t n = ::read(fd, buffer, sizeof buffer);
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                *error = posixError("read", errno);
                break;
            }
            if (n == 0)
                break;
            data.append(buffer, qsizetype(n));
            if (data.size() > maxBytes)
                break;
        }
        resolver.close(pfd);
        return data;
    }

private:
    // Seekable file: reserve the full size first (posix_fallocate), so running out of space is
    // found before any byte of the old contents changes; then overwrite, cut to length, fsync.
    static QString writeSeekable(int fd, qint64 originalSize, const QByteArray &bytes, bool *touched)
    {
        const qint64 length = bytes.size();
        const int reserve = ::posix_fallocate(fd, 0, off_t(length));
        qInfo().noquote() << QStringLiteral("[file] space reservation (posix_fallocate, %1 bytes): %2")
                                 .arg(length)
                                 .arg(reserve == 0 ? QStringLiteral("ok") : QString::fromLocal8Bit(std::strerror(reserve)));
        if (reserve == ENOSPC || reserve == EDQUOT) {
            struct stat st {};
            if (::fstat(fd, &st) == 0 && qint64(st.st_size) != originalSize)
                ::ftruncate(fd, off_t(originalSize)); // only the size can have changed, never the contents
            return QStringLiteral("There is not enough free space on the storage that holds this file.");
        }
        // Other results (for example EOPNOTSUPP on FUSE storage) mean no reservation is possible;
        // the free-space check before writing is then the only protection.
        for (qint64 done = 0; done < length;) {
            const ssize_t n = ::pwrite(fd, bytes.constData() + done, size_t(length - done), off_t(done));
            if (n < 0) {
                if (errno == EINTR)
                    continue;
                return QStringLiteral("Writing stopped (%1).").arg(posixError("write", errno));
            }
            *touched = true;
            done += n;
        }
        if (::ftruncate(fd, off_t(length)) != 0)
            return QStringLiteral("The file could not be cut to its new length (%1).").arg(posixError("ftruncate", errno));
        if (::fsync(fd) != 0 && errno != EINVAL && errno != EROFS)
            return QStringLiteral("The data could not be flushed to storage (%1).").arg(posixError("fsync", errno));
        return {};
    }
};

} // namespace

DocumentProvider *DocumentProvider::platform()
{
    static AndroidDocumentProvider provider;
    return &provider;
}

#endif
