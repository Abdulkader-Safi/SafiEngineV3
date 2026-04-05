/**
 * safi/core/log.h — tiny logging façade over SDL_Log.
 */
#ifndef SAFI_CORE_LOG_H
#define SAFI_CORE_LOG_H

#include <SDL3/SDL_log.h>

#define SAFI_LOG_INFO(...)  SDL_Log(__VA_ARGS__)
#define SAFI_LOG_WARN(...)  SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define SAFI_LOG_ERROR(...) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define SAFI_LOG_DEBUG(...) SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)

#endif /* SAFI_CORE_LOG_H */
