#include <string.h>
#include "Uri.h"

extern "C"
{
	#include "yuarel.h"
}

Uri::Uri() : Valid(false) { }

Uri::Uri(string uriStr)
{
	struct yuarel url;

	Valid = false;
	Scheme = "";
	Host = "";
	Path = "";

	memset(&url, 0, sizeof(struct yuarel));
	if (yuarel_parse(&url, &uriStr[0]) == -1) // §5 ARCH: &uriStr[0] on a value-param avoids UB const-cast
		return;

	if (url.scheme != NULL)
		Scheme = string(url.scheme);

	if (url.host != NULL)
		Host = string(url.host);

	if (url.path != NULL)
	{
		Path = string(url.path);

		if (url.query != NULL)
		{
			Path += "?" + string(url.query);
		}

		if (Path.empty() || Path[0] != '/') // §6 ARCH: removed redundant length check; Path[0] avoids one-char heap alloc
		{
			Path = "/" + Path;
		}
	}
	Valid = true;
}

bool Uri::IsAbsolute(const string& uriStr)
{
	/* Check first 4 chars case-insensitively — no copy, no transform, no heap */
	return uriStr.size() >= 4 &&
	       (uriStr[0] | 0x20) == 'h' &&
	       (uriStr[1] | 0x20) == 't' &&
	       (uriStr[2] | 0x20) == 't' &&
	       (uriStr[3] | 0x20) == 'p';
}

