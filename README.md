# pkg_fcgi - FastCGI interface for the OpenBSD ports tree

pkg_fcgi is meant to be the replacement of gempkg, a Python CGI script
that allows to browse the OpenBSD package archive via Gemini.

There's a hosted version updated daily available at
<gemini://gemini.omarpolo.com/cgi/gempkg/>.

pkg_fcgi depends on libevent and sqlite3.  To build, please run

	$ ./configure
	$ make
	$ doas make install

To operate, pkg_fcgi needs an augmented version of the sqlite database
installed by the sqlports package.  To generate it, issue;

	# install -d -o www /var/www/pkg_fcgi/
	# cp /usr/local/share/sqlports /var/www/pkg_fcgi/pkgs.sqlite3
	# sqlite3 /var/www/pkg_fcgi/pkgs.sqlite3 <schema.sql

A sample configuration for gmid is:

	server "localhost" {
		listen on *
		fastcgi socket "/run/pkg_fcgi.sock"
	}
