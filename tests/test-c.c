#include <config.h>
#include <stdlib.h>

#include "libtest.h"

int main()
{
	wget_test_url_t urls[] = {
		{
			.name = "/",
			.code = "200",
			.body = "<html><body>Test document</body></html>",
			.headers = {
				"Content-Type: text/html",
			}
		}
	};

	wget_test_start_server(
		WGET_TEST_RESPONSE_URLS, &urls, countof(urls),
		WGET_TEST_FEATURE_MHD,
		0);

	wget_test(
		WGET_TEST_OPTIONS, "-c",
		WGET_TEST_REQUEST_URL, "http://localhost:{{port}}",
		WGET_TEST_EXPECTED_ERROR_CODE, 0,
		WGET_TEST_EXISTING_FILES, &(wget_test_file_t []) {
			{ "index.html", urls[0].body },
			{	NULL } },
		WGET_TEST_EXPECTED_FILES, &(wget_test_file_t []) {
			{ "index.html", urls[0].body },
			{	NULL } },
		0);

	exit(0);
}
