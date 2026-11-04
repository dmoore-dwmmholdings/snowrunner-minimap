#pragma once
#include "native_settings.h"
#include <string>
enum class SrmmPreferencesResult { saved,busy,wrong_context,invalid,io_error };
// Control-thread only. Cooperates with Python session_config.config_lock.
SrmmPreferencesResult srmm_persist_display(const std::wstring& path,std::uint64_t session,
    const std::wstring& packet_path,const SrmmDisplaySettings&) noexcept;
