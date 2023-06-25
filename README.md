# pkg_fcgi - FastCGI interface for the OpenBSD package archive

pkg_fcgi is meant to be the replacement of gempkg, a Python CGI script
that allows to browse the OpenBSD package archive via Gemini.

pkg_fcgi depends on libevent and sqlite3.  To build, please run

	$ ./configure
	$ make
	$ doas make install

As pkg_fcgi is WIP, the documentation is too.
