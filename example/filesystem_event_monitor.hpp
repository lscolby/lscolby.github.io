/*
 // Example for for monitoring filesystem events
 */
#pragma once

#include <sys/inotify.h>

#include <boost/asio/io_context.hpp>

#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <span>
#include <string>

class FilesystemEventMonitor
{
  private:
    std::optional<boost::asio::posix::stream_descriptor> inotifyConn;

    int inotifyFd = -1;
    int dirWatchDesc = -1;
    int fileWatchDesc = -1;

    std::filesystem::path pathname{};

    bool addWatchOnDir();
    void rmWatchOnDir();
    bool addWatchOnFile();
    void rmWatchOnFile();
    std::string getEventString(int WatchDescriptor);
    void readEvents(std::span<char> readBuffer,
                    const std::size_t& bytesTransferred);
    void monitorFilesystemEvent();

  public:
    FilesystemEventMonitor(const FilesystemEventMonitor&) = delete;
    FilesystemEventMonitor& operator=(const FilesystemEventMonitor&) = delete;
    FilesystemEventMonitor(FilesystemEventMonitor&&) = delete;
    FilesystemEventMonitor& operator=(FilesystemEventMonitor&&) = delete;

    FilesystemEventMonitor(boost::asio::io_context& ioc,
                           std::filesystem::path pathname);
    ~FilesystemEventMonitor();
};

bool FilesystemEventMonitor::addWatchOnDir()
{
    dirWatchDesc = inotify_add_watch(inotifyFd, pathname.parent_path().c_str(),
                                     IN_ALL_EVENTS);
    if (dirWatchDesc == -1)
    {
        std::cerr << "inotify_add_watch failed for directory: "
                  << pathname.parent_path() << std::endl;
        return false;
    }

    std::cout << "Add inotify watcher on " << pathname.parent_path()
              << std::endl;
    return true;
}

void FilesystemEventMonitor::rmWatchOnDir()
{
    if (dirWatchDesc != -1)
    {
        std::cout << "Remove inotify watcher on "
                  << this->pathname.parent_path() << std::endl;
        inotify_rm_watch(inotifyFd, dirWatchDesc);
        dirWatchDesc = -1;
    }
}

bool FilesystemEventMonitor::addWatchOnFile()
{
    fileWatchDesc = inotify_add_watch(inotifyFd, pathname.c_str(),
                                      IN_ALL_EVENTS);
    if (fileWatchDesc == -1)
    {
        std::cerr << "inotify_add_watch failed for file: " << pathname.c_str()
                  << std::endl;
        return false;
    }

    std::cout << "Add inotify watcher on " << pathname.filename() << std::endl;
    return true;
}

void FilesystemEventMonitor::rmWatchOnFile()
{
    if (fileWatchDesc != -1)
    {
        std::cout << "Remove inotify watcher on " << pathname.filename()
                  << std::endl;
        inotify_rm_watch(inotifyFd, fileWatchDesc);
        fileWatchDesc = -1;
    }
}

FilesystemEventMonitor::FilesystemEventMonitor(boost::asio::io_context& ioc,
                                               std::filesystem::path pathname) :
    pathname(pathname)
{
    inotifyConn.emplace(ioc);
    if (!inotifyConn)
    {
        return;
    }

    inotifyFd = inotify_init1(IN_CLOEXEC | O_NONBLOCK);
    // inotifyFd = inotify_init1(O_NONBLOCK);
    if (inotifyFd == -1)
    {
        return;
    }

    // Add watch on directory to handle file create/delete.
    if (!addWatchOnDir())
    {
        return;
    }

    // Watch file for modifications.
    // Don't return error if file not exist. Watch on directory will handle
    // create/delete of file.
    (void)addWatchOnFile();

    // monitor filesystem
    inotifyConn->assign(inotifyFd);

    monitorFilesystemEvent();
}

FilesystemEventMonitor::~FilesystemEventMonitor()
{
    rmWatchOnDir();
    rmWatchOnFile();
    (void)close(inotifyFd);
}

std::string FilesystemEventMonitor::getEventString(int WatchDescriptor)
{
    switch (WatchDescriptor)
    {
        case IN_ACCESS:
            return "IN_ACCESS - File was accessed.";
            break;
        case IN_ATTRIB:
            return "IN_ATTRIB - Metadata changed.";
            break;
        case IN_CLOSE_WRITE:
            return "IN_CLOSE_WRITE - File opened for writing was closed.";
            break;
        case IN_CLOSE_NOWRITE:
            return "IN_CLOSE_NOWRITE - File or directory not opened for "
                   "writing was closed.";
            break;
        case IN_CREATE:
            return "IN_CREATE - File/directory created in watched directory.";
            break;
        case IN_DELETE:
            return "IN_DELETE - File/directory deleted from watched directory.";
            break;
        case IN_DELETE_SELF:
            return "IN_DELETE_SELF - Watched file/directory was itself "
                   "deleted.";
            break;
        case IN_MODIFY:
            return "IN_MODIFY - File was modified.";
        case IN_MOVE_SELF:
            return "IN_MOVE_SELF -  Watched file/directory was itself moved.";
            break;
        case IN_MOVED_FROM:
            return "IN_MOVED_FROM - Generated for the directory containing the "
                   "old filename when a file is renamed.";
            break;
        case IN_MOVED_TO:
            return "IN_MOVED_TO - Generated for the directory containing the "
                   "new filename when a file is renamed.";
            break;
        case IN_OPEN:
            return "IN_OPEN - File or directory was opened.";
            break;

        default:
            break;
    }

    return "";
}

void FilesystemEventMonitor::readEvents(std::span<char> readBuffer,
                                        const std::size_t& bytesTransferred)
{
    constexpr const size_t iEventSize = sizeof(inotify_event);

    std::size_t index = 0;
    while ((index + iEventSize) <= bytesTransferred)
    {
        struct inotify_event event
        {};
        std::memcpy(&event, &readBuffer[index], iEventSize);

        if (event.wd == dirWatchDesc)
        {
            if ((event.len == 0) ||
                (index + iEventSize + event.len > bytesTransferred))
            {
                index += (iEventSize + event.len);
                continue;
            }

            std::string filename(&readBuffer[index + iEventSize]);

            if (filename != pathname.filename())
            {
                index += (iEventSize + event.len);
                continue;
            }

            std::cout << filename << " inside " << pathname.parent_path()
                      << "\n    " << getEventString(event.mask) << std::endl;

            switch (event.mask)
            {
                case IN_CREATE:
                    // Remove existing inotify watcher and add with new file.
                    rmWatchOnFile();
                    if (!addWatchOnFile())
                    {
                        return;
                    }

                    break;
                case IN_DELETE:
                case IN_MOVED_TO:
                    rmWatchOnFile();
                    break;

                default:
                    break;
            }
        }
        else if (event.wd == fileWatchDesc)
        {
            std::cout << pathname.filename() << "\n    "
                      << getEventString(event.mask) << std::endl;
        }

        index += (iEventSize + event.len);
    }
}

void FilesystemEventMonitor::monitorFilesystemEvent()
{
    static std::array<char, 1024> readBuffer;

    inotifyConn->async_read_some(boost::asio::buffer(readBuffer),
                                 [&](const boost::system::error_code& ec,
                                     const std::size_t& bytesTransferred) {
        if (ec)
        {
            std::cerr << "Callback Error: " << ec.message() << std::endl;
            return;
        }

        readEvents(readBuffer, bytesTransferred);
        monitorFilesystemEvent();
    });
}
