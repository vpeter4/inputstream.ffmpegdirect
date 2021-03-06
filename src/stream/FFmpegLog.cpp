/*
 *  Copyright (C) 2005-2020 Team Kodi
 *  https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSE.md for more information.
 */

#include "FFmpegLog.h"

#include "threads/CriticalSection.h"
#include "threads/Thread.h"
#include "../utils/Log.h"

#include <map>

#include <kodi/General.h>
#include <p8-platform/util/StringUtils.h>

using namespace ffmpegdirect;

static thread_local FFmpegLog* CFFmpegDirectLogTls;

void FFmpegLog::SetLogLevel(int level)
{
  FFmpegLog::ClearLogLevel();
  FFmpegLog *log = new FFmpegLog();
  log->level = level;
  CFFmpegDirectLogTls = log;
}

int FFmpegLog::GetLogLevel()
{
  FFmpegLog* log = CFFmpegDirectLogTls;
  if (!log)
    return -1;
  return log->level;
}

void FFmpegLog::ClearLogLevel()
{
  FFmpegLog* log = CFFmpegDirectLogTls;
  CFFmpegDirectLogTls = nullptr;
  if (log)
    delete log;
}

static CCriticalSection m_ffmpegdirectLogSection;
std::map<const CThread*, std::string> g_ffmpegdirectLogbuffer;

void ff_flush_avutil_log_buffers(void)
{
  CSingleLock lock(m_ffmpegdirectLogSection);
  /* Loop through the logbuffer list and remove any blank buffers
     If the thread using the buffer is still active, it will just
     add a new buffer next time it writes to the log */
  std::map<const CThread*, std::string>::iterator it;
  for (it = g_ffmpegdirectLogbuffer.begin(); it != g_ffmpegdirectLogbuffer.end(); )
    if ((*it).second.empty())
      g_ffmpegdirectLogbuffer.erase(it++);
    else
      ++it;
}

void ff_avutil_log(void* ptr, int level, const char* format, va_list va)
{
  CSingleLock lock(m_ffmpegdirectLogSection);
  const CThread* threadId = CThread::GetCurrentThread();
  std::string &buffer = g_ffmpegdirectLogbuffer[threadId];

  AVClass* avc= ptr ? *(AVClass**)ptr : NULL;

  int maxLevel = AV_LOG_WARNING;
  if (FFmpegLog::GetLogLevel() > 0)
    maxLevel = AV_LOG_INFO;

  if (level > maxLevel && !kodi::GetSettingBoolean("allowFFmpegLogging"))
    return;

  LogLevel type;
  switch (level)
  {
    case AV_LOG_INFO:
      type = LOGLEVEL_INFO;
      break;

    case AV_LOG_ERROR:
      type = LOGLEVEL_ERROR;
      break;

    case AV_LOG_DEBUG:
    default:
      type = LOGLEVEL_DEBUG;
      break;
  }

  std::string message = StringUtils::FormatV(format, va);
  std::string prefix = StringUtils::Format("ffmpeg[%pX]: ", static_cast<const void*>(threadId));
  if (avc)
  {
    if (avc->item_name)
      prefix += std::string("[") + avc->item_name(ptr) + "] ";
    else if (avc->class_name)
      prefix += std::string("[") + avc->class_name + "] ";
  }

  buffer += message;
  int pos, start = 0;
  while ((pos = buffer.find_first_of('\n', start)) >= 0)
  {
    if (pos > start)
      Log(type, "%s%s", prefix.c_str(), buffer.substr(start, pos - start).c_str());
    start = pos+1;
  }
  buffer.erase(0, start);
}

