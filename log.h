/*
 * This is free and unencumbered software released into the public domain.
 *
 * Anyone is free to copy, modify, publish, use, compile, sell, or
 * distribute this software, either in source code form or as a compiled
 * binary, for any purpose, commercial or non-commercial, and by any
 * means.
 */

#define LOG_ATTR_PRINTF(A, B) __attribute__((__format__ (printf, A, B)))
struct logger {
	__dead void (*fatal)(int, const char *, ...)	LOG_ATTR_PRINTF(2, 3);
	__dead void (*fatalx)(int, const char *, ...)	LOG_ATTR_PRINTF(2, 3);
	void (*warn)(const char *, ...)			LOG_ATTR_PRINTF(1, 2);
	void (*warnx)(const char *, ...)		LOG_ATTR_PRINTF(1, 2);
	void (*info)(const char *, ...)			LOG_ATTR_PRINTF(1, 2);
	void (*debug)(const char *, ...)		LOG_ATTR_PRINTF(1, 2);
};
#undef LOG_ATTR_PRINTF

extern const struct logger *logger, syslogger, dbglogger;

#define fatal(...)	logger->fatal(1, __VA_ARGS__)
#define fatalx(...)	logger->fatalx(1, __VA_ARGS__)
#define log_warn(...)	logger->warn(__VA_ARGS__)
#define log_warnx(...)	logger->warnx(__VA_ARGS__)
#define log_info(...)	logger->info(__VA_ARGS__)
#define log_debug(...)	logger->debug(__VA_ARGS__)

void	log_init(int, int);
void	log_setverbose(int);
