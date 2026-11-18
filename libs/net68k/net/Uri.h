#ifndef __URI__
#define __URI__

#include <string>

using namespace std;

class Uri
{
public:
	Uri();
	Uri(string uriStr);
	bool Valid;
	string Scheme;
	string Host;
	string Path;
	int Port;                 /* 0 when the URL did not name one */
	/* Host for the Host: header - includes the port when it is not the
	 * scheme default, because virtual hosts behind a non-443 port need it. */
	string HostHeader() const;
	static bool IsAbsolute(const string& uriStr);
};

#endif