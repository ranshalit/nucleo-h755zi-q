/* SPDX-License-Identifier: Apache-2.0 */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "speech_service.h"
int main(void)
{
	char buffer[1024];
	struct speech_snapshot snapshot = {
		.state = SPEECH_UNAVAILABLE, .reason = "mic \"gone\"\n"
	};
	int n = speech_snapshot_json(&snapshot, buffer, sizeof(buffer));
	assert(n > 0 && (size_t)n == strlen(buffer));
	assert(strstr(buffer, "\"label\":null,\"score\":null"));
	assert(strstr(buffer, "\"last_event\":null"));
	assert(strstr(buffer, "mic \\\"gone\\\"\\u000a"));
	assert(speech_snapshot_json(&snapshot, buffer, 10) == -ENOSPC);
	snapshot.state = SPEECH_RUNNING;
	snapshot.reason = "recognizing English yes/no";
	snapshot.prediction_valid = true;
	snapshot.label = SPEECH_YES;
	snapshot.score_permille = 910;
	snapshot.event_valid = true;
	snapshot.event_label = SPEECH_NO;
	snapshot.event_sequence = 7;
	snapshot.event_uptime_ms = INT64_C(5000000000);
	n = speech_snapshot_json(&snapshot, buffer, sizeof(buffer));
	assert(n > 0);
	assert(strstr(buffer, "\"label\":\"yes\",\"score\":0.910"));
	assert(strstr(buffer, "\"last_event\":{\"label\":\"no\",\"sequence\":7,\"uptime_ms\":5000000000}"));
	puts(buffer);
	snapshot.score_permille = 1001;
	assert(speech_snapshot_json(&snapshot, buffer, sizeof(buffer)) == -EINVAL);
	puts("PASS: snapshot JSON nulls, escaping, score, uptime and overflow");
	return 0;
}
