/*-
 * Copyright 2016 Vsevolod Stakhov
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "lua_common.h"
#include "message.h"
#include "images.h"
#include "archives.h"
#include "utlist.h"
#include "unix-std.h"
#include "libmime/smtp_parsers.h"
#include <math.h>

/***
 * @module rspamd_task
 * This module provides routines for tasks manipulation in rspamd. Tasks usually
 * represent messages being scanned, and this API provides access to such elements
 * as headers, symbols, metrics and so on and so forth. Normally, task objects
 * are passed to the lua callbacks allowing to check specific properties of messages
 * and add the corresponding symbols to the scan's results.
@example
rspamd_config.DATE_IN_PAST = function(task)
	local dm = task:get_date{format = 'message', gmt = true}
	local dt = task:get_date{format = 'connect', gmt = true}
	-- A day
	if dt - dm > 86400 then
		return true
	end

	return false
end
 */

/* Task methods */
LUA_FUNCTION_DEF (task, get_message);
LUA_FUNCTION_DEF (task, process_message);
/***
 * @method task:get_cfg()
 * Get configuration object for a task.
 * @return {rspamd_config} (config.md)[configuration object] for the task
 */
LUA_FUNCTION_DEF (task, get_cfg);
LUA_FUNCTION_DEF (task, set_cfg);
LUA_FUNCTION_DEF (task, destroy);
/***
 * @method task:get_mempool()
 * Returns memory pool valid for a lifetime of task. It is used internally by
 * many rspamd routines.
 * @return {rspamd_mempool} memory pool object
 */
LUA_FUNCTION_DEF (task, get_mempool);
/***
 * @method task:get_session()
 * Returns asynchronous session object that is used by many rspamd asynchronous
 * utilities internally.
 * @return {rspamd_session} session object
 */
LUA_FUNCTION_DEF (task, get_session);
/***
 * @method task:get_ev_base()
 * Return asynchronous event base for using in callbacks and resolver.
 * @return {rspamd_ev_base} event base
 */
LUA_FUNCTION_DEF (task, get_ev_base);
/***
 * @method task:insert_result(symbol, weigth[, option1, ...])
 * Insert specific symbol to the tasks scanning results assigning the initial
 * weight to it.
 * @param {string} symbol symbol to insert
 * @param {number} weight initial weight (this weight is multiplied by the metric weight)
 * @param {string} options list of optional options attached to a symbol inserted
@example
local function cb(task)
	if task:get_header('Some header') then
		task:insert_result('SOME_HEADER', 1.0, 'Got some header')
	end
end
 */
LUA_FUNCTION_DEF (task, insert_result);
/***
 * @method task:set_pre_result(action, description)
 * Sets pre-result for a task. It is used in pre-filters to specify early result
 * of the task scanned. If a pre-filter sets some result, then further processing
 * may be skipped. For selecting action it is possible to use global table
 * `rspamd_actions` or a string value:
 *
 * - `reject`: reject message permanently
 * - `add header`: add spam header
 * - `rewrite subject`: rewrite subject to spam subject
 * - `greylist`: greylist message
 * - `accept` or `no action`: whitelist message
 * @param {rspamd_action or string} action a numeric or string action value
 * @param {string} description optional descripton
@example
local function cb(task)
	local gr = task:get_header('Greylist')
	if gr and gr == 'greylist' then
		task:set_pre_result(rspamd_actions['greylist'], 'Greylisting required')
	end
end
 */
LUA_FUNCTION_DEF (task, set_pre_result);
/***
 * @method task:append_message(message)
 * Adds a message to scanning output.
 * @param {string} message
@example
local function cb(task)
	task:append_message('Example message')
end
 */
LUA_FUNCTION_DEF (task, append_message);
/***
 * @method task:get_urls([need_emails])
 * Get all URLs found in a message.
 * @param {boolean} need_emails if `true` then reutrn also email urls
 * @return {table rspamd_url} list of all urls found
@example
local function phishing_cb(task)
	local urls = task:get_urls();

	if urls then
		for _,url in ipairs(urls) do
			if url:is_phished() then
				return true
			end
		end
	end
	return false
end
 */
LUA_FUNCTION_DEF (task, get_urls);
/***
 * @method task:has_urls([need_emails])
 * Returns 'true' if a task has urls listed
 * @param {boolean} need_emails if `true` then reutrn also email urls
 * @return {boolean} true if a task has urls (urls or emails if `need_emails` is true)
 */
LUA_FUNCTION_DEF (task, has_urls);
/***
 * @method task:get_content()
 * Get raw content for the specified task
 * @return {text} the data contained in the task
 */
LUA_FUNCTION_DEF (task, get_content);

/***
 * @method task:get_rawbody()
 * Get raw body for the specified task
 * @return {text} the data contained in the task
 */
LUA_FUNCTION_DEF (task, get_rawbody);

/***
 * @method task:get_emails()
 * Get all email addresses found in a message.
 * @return {table rspamd_url} list of all email addresses found
 */
LUA_FUNCTION_DEF (task, get_emails);
/***
 * @method task:get_text_parts()
 * Get all text (and HTML) parts found in a message
 * @return {table rspamd_text_part} list of text parts
 */
LUA_FUNCTION_DEF (task, get_text_parts);
/***
 * @method task:get_parts()
 * Get all mime parts found in a message
 * @return {table rspamd_mime_part} list of mime parts
 */
LUA_FUNCTION_DEF (task, get_parts);

/***
 * @method task:get_request_header(name)
 * Get value of a HTTP request header.
 * @param {string} name name of header to get
 * @return {rspamd_text} value of an HTTP header
 */
LUA_FUNCTION_DEF (task, get_request_header);
/***
 * @method task:set_request_header(name, value)
 * Set value of a HTTP request header. If value is omitted, then a header is removed
 * @param {string} name name of header to get
 * @param {rspamd_text/string} value new header's value
 */
LUA_FUNCTION_DEF (task, set_request_header);
/***
 * @method task:get_subject()
 * Returns task subject (either from the protocol override or from a header)
 * @return {string} value of a subject (decoded)
 */
LUA_FUNCTION_DEF (task, get_subject);
/***
 * @method task:get_header(name[, case_sensitive])
 * Get decoded value of a header specified with optional case_sensitive flag.
 * By default headers are searched in caseless matter.
 * @param {string} name name of header to get
 * @param {boolean} case_sensitive case sensitiveness flag to search for a header
 * @return {string} decoded value of a header
 */
LUA_FUNCTION_DEF (task, get_header);
/***
 * @method task:get_header_raw(name[, case_sensitive])
 * Get raw value of a header specified with optional case_sensitive flag.
 * By default headers are searched in caseless matter.
 * @param {string} name name of header to get
 * @param {boolean} case_sensitive case sensitiveness flag to search for a header
 * @return {string} raw value of a header
 */
LUA_FUNCTION_DEF (task, get_header_raw);
/***
 * @method task:get_header_full(name[, case_sensitive])
 * Get raw value of a header specified with optional case_sensitive flag.
 * By default headers are searched in caseless matter. This method returns more
 * information about the header as a list of tables with the following structure:
 *
 * - `name` - name of a header
 * - `value` - raw value of a header
 * - `decoded` - decoded value of a header
 * - `tab_separated` - `true` if a header and a value are separated by `tab` character
 * - `empty_separator` - `true` if there are no separator between a header and a value
 * @param {string} name name of header to get
 * @param {boolean} case_sensitive case sensitiveness flag to search for a header
 * @return {list of tables} all values of a header as specified above
@example
function check_header_delimiter_tab(task, header_name)
	for _,rh in ipairs(task:get_header_full(header_name)) do
		if rh['tab_separated'] then return true end
	end
	return false
end
 */
LUA_FUNCTION_DEF (task, get_header_full);

/***
 * @method task:get_raw_headers()
 * Get all undecoded headers of a message as a string
 * @return {rspamd_text} all raw headers for a message as opaque text
 */
LUA_FUNCTION_DEF (task, get_raw_headers);

/***
 * @method task:get_received_headers()
 * Returns a list of tables of parsed received headers. A tables returned have
 * the following structure:
 *
 * - `from_hostname` - string that represents hostname provided by a peer
 * - `from_ip` - string representation of IP address as provided by a peer
 * - `real_hostname` - hostname as resolved by MTA
 * - `real_ip` - string representation of IP as resolved by PTR request of MTA
 * - `by_hostname` - MTA hostname
 * - `proto` - protocol, e.g. ESMTP or ESMTPS
 * - `timestamp` - received timetamp
 * - `for` - for value (unparsed mailbox)
 *
 * Please note that in some situations rspamd cannot parse all the fields of received headers.
 * In that case you should check all strings for validity.
 * @return {table of tables} list of received headers described above
 */
LUA_FUNCTION_DEF (task, get_received_headers);
/***
 * @method task:get_queue_id()
 * Returns queue ID of the message being processed.
 */
LUA_FUNCTION_DEF (task, get_queue_id);
/***
 * @method task:get_uid()
 * Returns ID of the task being processed.
 */
LUA_FUNCTION_DEF (task, get_uid);
/***
 * @method task:get_resolver()
 * Returns ready to use rspamd_resolver object suitable for making asynchronous DNS requests.
 * @return {rspamd_resolver} resolver object associated with the task's session
 * @example
local logger = require "rspamd_logger"

local function task_cb(task)
	local function dns_cb(resolver, to_resolve, results, err)
		-- task object is available due to closure
		task:inc_dns_req()
		if results then
			logger.info(string.format('<%s> [%s] resolved for symbol: %s',
				task:get_message_id(), to_resolve, 'EXAMPLE_SYMBOL'))
			task:insert_result('EXAMPLE_SYMBOL', 1)
		end
	end
	local r = task:get_resolver()
	r:resolve_a(task:get_session(), task:get_mempool(), 'example.com', dns_cb)
end
 */
LUA_FUNCTION_DEF (task, get_resolver);
/***
 * @method task:inc_dns_req()
 * Increment number of DNS requests for the task. Is used just for logging purposes.
 */
LUA_FUNCTION_DEF (task, inc_dns_req);
/***
 * @method task:get_dns_req()
 * Get number of dns requests being sent in the task
 * @return {number} number of DNS requests
 */
LUA_FUNCTION_DEF (task, get_dns_req);

/***
 * @method task:has_recipients([type])
 * Return true if there are SMTP or MIME recipients for a task.
 * @param {integer|string} type if specified has the following meaning: `0` or `any` means try SMTP recipients and fallback to MIME if failed, `1` or `smtp` means checking merely SMTP recipients and `2` or `mime` means MIME recipients only
 * @return {bool} `true` if there are recipients of the following type
 */
LUA_FUNCTION_DEF (task, has_recipients);

/***
 * @method task:get_recipients([type])
 * Return SMTP or MIME recipients for a task. This function returns list of internet addresses each one is a table with the following structure:
 *
 * - `name` - name of internet address in UTF8, e.g. for `Vsevolod Stakhov <blah@foo.com>` it returns `Vsevolod Stakhov`
 * - `addr` - address part of the address
 * - `user` - user part (if present) of the address, e.g. `blah`
 * - `domain` - domain part (if present), e.g. `foo.com`
 * @param {integer|string} type if specified has the following meaning: `0` or `any` means try SMTP recipients and fallback to MIME if failed, `1` or `smtp` means checking merely SMTP recipients and `2` or `mime` means MIME recipients only
 * @return {list of addresses} list of recipients or `nil`
 */
LUA_FUNCTION_DEF (task, get_recipients);

/***
 * @method task:set_recipients([type], {rcpt1, rcpt2...})
 * Sets sender for a task. This function accepts table that will be converted to the address.
 * If some fields are missing they are subsequently reconstructed by this function. E.g. if you
 * specify 'user' and 'domain', then address and raw string will be reconstructed
 *
 * - `name` - name of internet address in UTF8, e.g. for `Vsevolod Stakhov <blah@foo.com>` it returns `Vsevolod Stakhov`
 * - `addr` - address part of the address
 * - `user` - user part (if present) of the address, e.g. `blah`
 * - `domain` - domain part (if present), e.g. `foo.com`
 * @param {integer|string} type if specified has the following meaning: `0` or `any` means try SMTP recipients and fallback to MIME if failed, `1` or `smtp` means checking merely SMTP recipients and `2` or `mime` means MIME recipients only
 * @param {list of tables} recipients recipients to set
 * @return {boolean} result of the operation
 */
LUA_FUNCTION_DEF (task, set_recipients);

/***
 * @method task:has_from([type])
 * Return true if there is SMTP or MIME sender for a task.
 * @param {integer|string} type if specified has the following meaning: `0` or `any` means try SMTP recipients and fallback to MIME if failed, `1` or `smtp` means checking merely SMTP recipients and `2` or `mime` means MIME recipients only
 * @return {bool} `true` if there is sender of the following type
 */
LUA_FUNCTION_DEF (task, has_from);

/***
 * @method task:get_from([type])
 * Return SMTP or MIME sender for a task. This function returns an internet address which one is a table with the following structure:
 *
 * - `name` - name of internet address in UTF8, e.g. for `Vsevolod Stakhov <blah@foo.com>` it returns `Vsevolod Stakhov`
 * - `addr` - address part of the address
 * - `user` - user part (if present) of the address, e.g. `blah`
 * - `domain` - domain part (if present), e.g. `foo.com`
 * @param {integer|string} type if specified has the following meaning: `0` or `any` means try SMTP sender and fallback to MIME if failed, `1` or `smtp` means checking merely SMTP sender and `2` or `mime` means MIME `From:` only
 * @return {address} sender or `nil`
 */
LUA_FUNCTION_DEF (task, get_from);

/***
 * @method task:set_from(type, addr)
 * Sets sender for a task. This function accepts table that will be converted to the address.
 * If some fields are missing they are subsequently reconstructed by this function. E.g. if you
 * specify 'user' and 'domain', then address and raw string will be reconstructed
 *
 * - `name` - name of internet address in UTF8, e.g. for `Vsevolod Stakhov <blah@foo.com>` it returns `Vsevolod Stakhov`
 * - `addr` - address part of the address
 * - `user` - user part (if present) of the address, e.g. `blah`
 * - `domain` - domain part (if present), e.g. `foo.com`
 * @param {integer|string} type if specified has the following meaning: `0` or `any` means try SMTP sender and fallback to MIME if failed, `1` or `smtp` means checking merely SMTP sender and `2` or `mime` means MIME `From:` only
 * @param {table
 * @return {boolean} success or not
 */
LUA_FUNCTION_DEF (task, set_from);
/***
 * @method task:get_user()
 * Returns authenticated user name for this task if specified by an MTA.
 * @return {string} username or nil
 */
LUA_FUNCTION_DEF (task, get_user);
LUA_FUNCTION_DEF (task, set_user);
/***
 * @method task:get_from_ip()
 * Returns [ip_addr](ip.md) object of a sender that is provided by MTA
 * @return {rspamd_ip} ip address object
 */
LUA_FUNCTION_DEF (task, get_from_ip);
/***
 * @method task:set_from_ip(str)
 * Set tasks's IP address based on the passed string
 * @param {string} str string representation of ip
 */
LUA_FUNCTION_DEF (task, set_from_ip);
LUA_FUNCTION_DEF (task, get_from_ip_num);
/***
 * @method task:get_client_ip()
 * Returns [ip_addr](ip.md) object of a client connected to rspamd (normally, it is an IP address of MTA)
 * @return {rspamd_ip} ip address object
 */
LUA_FUNCTION_DEF (task, get_client_ip);
/***
 * @method task:get_helo()
 * Returns the value of SMTP helo provided by MTA.
 * @return {string} HELO value
 */
LUA_FUNCTION_DEF (task, get_helo);
LUA_FUNCTION_DEF (task, set_helo);
/***
 * @method task:get_hostname()
 * Returns the value of sender's hostname provided by MTA
 * @return {string} hostname value
 */
LUA_FUNCTION_DEF (task, get_hostname);
LUA_FUNCTION_DEF (task, set_hostname);
/***
 * @method task:get_images()
 * Returns list of all images found in a task as a table of `rspamd_image`.
 * Each image has the following methods:
 *
 * * `get_width` - return width of an image in pixels
 * * `get_height` - return height of an image in pixels
 * * `get_type` - return string representation of image's type (e.g. 'jpeg')
 * * `get_filename` - return string with image's file name
 * * `get_size` - return size in bytes
 * @return {list of rspamd_image} images found in a message
 */
LUA_FUNCTION_DEF (task, get_images);
/***
 * @method task:get_archives()
 * Returns list of all archives found in a task as a table of `rspamd_archive`.
 * Each archive has the following methods available:
 *
 * * `get_files` - return list of strings with filenames inside archive
 * * `get_files_full` - return list of tables with all information about files
 * * `is_encrypted` - return true if an archive is encrypted
 * * `get_type` - return string representation of image's type (e.g. 'zip')
 * * `get_filename` - return string with archive's file name
 * * `get_size` - return size in bytes
 * @return {list of rspamd_archive} archives found in a message
 */
LUA_FUNCTION_DEF (task, get_archives);
/***
 * @method task:get_symbol(name)
 * Searches for a symbol `name` in all metrics results and returns a list of tables
 * one per metric that describes the symbol inserted. Please note that this function
 * is intended to return values for **inserted** symbols, so if this symbol was not
 * inserted it won't be in the function's output. This method is useful for post-filters mainly.
 * The symbols are returned as the list of the following tables:
 *
 * - `metric` - name of metric
 * - `score` - score of a symbol in that metric
 * - `options` - a table of strings representing options of a symbol
 * - `group` - a group of symbol (or 'ungrouped')
 * @param {string} name symbol's name
 * @return {list of tables} list of tables or nil if symbol was not found in any metric
 */
LUA_FUNCTION_DEF (task, get_symbol);
/***
 * @method task:get_symbols_all()
 * Returns array of symbols matched in default metric with all metadata
 * @return {table} table of tables formatted as in `task:get_symbol()` except that `metric` is absent and `name` is added
 */
LUA_FUNCTION_DEF (task, get_symbols_all);
/***
 * @method task:get_symbols()
 * Returns array of all symbols matched for this task
 * @return {table, table} table of strings with symbols names + table of theirs scores
 */
LUA_FUNCTION_DEF (task, get_symbols);

/***
 * @method task:get_symbols_numeric()
 * Returns array of all symbols matched for this task
 * @return {table|number, table|number} table of numbers with symbols ids + table of theirs scores
 */
LUA_FUNCTION_DEF (task, get_symbols_numeric);

/***
 * @method task:has_symbol(name)
 * Fast path to check if a specified symbol is in the task's results
 * @param {string} name symbol's name
 * @return {boolean} `true` if symbol has been found
 */
LUA_FUNCTION_DEF (task, has_symbol);
/***
 * @method task:get_date(type[, gmt])
 * Returns timestamp for a connection or for a MIME message. This function can be called with a
 * single table arguments with the following fields:
 *
 * * `format` - a format of date returned:
 * 	- `message` - returns a mime date as integer (unix timestamp)
 * 	- `message_str` - returns a mime date as string (UTC format)
 * 	- `connect` - returns a unix timestamp of a connection to rspamd
 * 	- `connect_str` - returns connection time in UTC format
 * * `gmt` - returns date in `GMT` timezone (normal for unix timestamps)
 *
 * By default this function returns connection time in numeric format.
 * @param {string} type date format as described above
 * @param {boolean} gmt gmt flag as described above
 * @return {string/number} date representation according to format
 * @example
rspamd_config.DATE_IN_PAST = function(task)
	local dm = task:get_date{format = 'message', gmt = true}
	local dt = task:get_date{format = 'connect', gmt = true}
	-- A day
	if dt - dm > 86400 then
		return true
	end

	return false
end
 */
LUA_FUNCTION_DEF (task, get_date);
/***
 * @method task:get_message_id()
 * Returns message id of the specified task
 * @return {string} if of a message
 */
LUA_FUNCTION_DEF (task, get_message_id);
LUA_FUNCTION_DEF (task, get_timeval);
/***
 * @method task:get_metric_score(name)
 * Get the current score of metric `name`. Should be used in post-filters only.
 * @param {string} name name of a metric
 * @return {table} table containing the current score and required score of the metric
 */
LUA_FUNCTION_DEF (task, get_metric_score);
/***
 * @method task:get_metric_action(name)
 * Get the current action of metric `name`. Should be used in post-filters only.
 * @param {string} name name of a metric
 * @return {string} the current action of the metric as a string
 */
LUA_FUNCTION_DEF (task, get_metric_action);
/***
 * @method task:set_metric_score(name, score)
 * Set the current score of metric `name`. Should be used in post-filters only.
 * @param {string} name name of a metric
 * @param {number} score the current score of the metric
 */
LUA_FUNCTION_DEF (task, set_metric_score);
/***
 * @method task:set_metric_action(name, action)
 * Set the current action of metric `name`. Should be used in post-filters only.
 * @param {string} name name of a metric
 * @param {string} action name to set
 */
LUA_FUNCTION_DEF (task, set_metric_action);
/***
 * @method task:set_metric_subject(subject)
 * Set the subject in the default metric
 * @param {string} subject subject to set
 */
LUA_FUNCTION_DEF (task, set_metric_subject);

/***
 * @method task:learn(is_spam[, classifier)
 * Learn classifier `classifier` with the task. If `is_spam` is true then message
 * is learnt as spam. Otherwise HAM is learnt. By default, this function learns
 * `bayes` classifier.
 * @param {boolean} is_spam learn spam or ham
 * @param {string} classifier classifier's name
 * @return {boolean} `true` if classifier has been learnt successfully
 */
LUA_FUNCTION_DEF (task, learn);
/***
 * @method task:set_settings(obj)
 * Set users settings object for a task. The format of this object is described
 * [here](https://rspamd.com/doc/configuration/settings.html).
 * @param {any} obj any lua object that corresponds to the settings format
 */
LUA_FUNCTION_DEF (task, set_settings);

/***
 * @method task:get_settings()
 * Gets users settings object for a task. The format of this object is described
 * [here](https://rspamd.com/doc/configuration/settings.html).
 * @return {lua object} lua object generated from UCL
 */
LUA_FUNCTION_DEF (task, get_settings);

/***
 * @method task:lookup_settings(key)
 * Gets users settings object with the specified key for a task.
 * @param {string} key key to lookup
 * @return {lua object} lua object generated from UCL
 */
LUA_FUNCTION_DEF (task, lookup_settings);

/***
 * @method task:get_settings_id()
 * Get numeric hash of settings id if specified for this task. 0 is returned otherwise.
 * @return {number} settings-id hash
 */
LUA_FUNCTION_DEF (task, get_settings_id);

/***
 * @method task:set_rmilter_reply(obj)
 * Set special reply for rmilter
 * @param {any} obj any lua object that corresponds to the settings format
 * @example
task:set_rmilter_reply({
	add_headers = {['X-Lua'] = 'test'},
	-- 1 is the position of header to remove
	remove_headers = {['DKIM-Signature'] = 1},
})
 */
LUA_FUNCTION_DEF (task, set_rmilter_reply);

/***
 * @method task:process_re(params)
 * Processes the specified regexp and returns number of captures (cached or new)
 * Params is the table with the follwoing fields (mandatory fields are marked with `*`):
 * - `re`* : regular expression object
 * - `type`*: type of regular expression:
 *   + `mime`: mime regexp
 *   + `header`: header regexp
 *   + `rawheader`: raw header expression
 *   + `rawmime`: raw mime regexp
 *   + `body`: raw body regexp
 *   + `url`: url regexp
 * - `header`: for header and rawheader regexp means the name of header
 * - `strong`: case sensitive match for headers
 * @return {number} number of regexp occurences in the task (limited by 255 so far)
 */
LUA_FUNCTION_DEF (task, process_regexp);

/***
 * @method task:cache_set(key, value)
 * Store some value to the task cache
 * @param {string} key key to use
 * @param {any} value any value (including functions and tables)
 */
LUA_FUNCTION_DEF (task, cache_set);
/***
 * @method task:cache_get(key)
 * Returns cached value or nil if nothing is cached
 * @param {string} key key to use
 * @return {any} cached value
 */
LUA_FUNCTION_DEF (task, cache_get);

/***
 * @method task:get_size()
 * Returns size of the task in bytes (that includes headers + parts size)
 * @return {number} size in bytes
 */
LUA_FUNCTION_DEF (task, get_size);

/***
 * @method task:set_flag(flag_name[, set])
 * Set specific flag for task:
 *
 * - `no_log`: do not log task summary
 * - `no_stat`: do not include task into scanned stats
 * - `pass_all`: check all filters for task
 * - `extended_urls`: output extended info about urls
 * - `skip`: skip task processing
 * - `learn_spam`: learn message as spam
 * - `learn_ham`: learn message as ham
 * - `broken_headers`: header data is broken for a message
 * @param {string} flag to set
 * @param {boolean} set set or clear flag (default is set)
@example
--[[
For messages with undefined queue ID (scanned with rspamc or WebUI)
do not include results into statistics and do not log task summary
(it will not appear in the WebUI history as well).
]]--

-- Callback function to set flags
local function no_log_stat_cb(task)
  if not task:get_queue_id() then
    task:set_flag('no_log')
    task:set_flag('no_stat')
  end
end

rspamd_config:register_symbol({
  name = 'LOCAL_NO_LOG_STAT',
  type = 'postfilter',
  callback = no_log_stat_cb
})
 */
LUA_FUNCTION_DEF (task, set_flag);


/***
 * @method task:has_flag(flag_name)
 * Checks for a specific flag in task:
 *
 * - `no_log`: do not log task summary
 * - `no_stat`: do not include task into scanned stats
 * - `pass_all`: check all filters for task
 * - `extended_urls`: output extended info about urls
 * - `skip`: skip task processing
 * - `learn_spam`: learn message as spam
 * - `learn_ham`: learn message as ham
 * - `broken_headers`: header data is broken for a message
 * @param {string} flag to check
 * @return {boolean} true if flags is set
 */
LUA_FUNCTION_DEF (task, has_flag);


/***
 * @method task:get_flags()
 * Get list of flags for task:
 *
 * - `no_log`: do not log task summary
 * - `no_stat`: do not include task into scanned stats
 * - `pass_all`: check all filters for task
 * - `extended_urls`: output extended info about urls
 * - `skip`: skip task processing
 * - `learn_spam`: learn message as spam
 * - `learn_ham`: learn message as ham
 * - `broken_headers`: header data is broken for a message
 * @return {array of strings} table with all flags as strings
 */
LUA_FUNCTION_DEF (task, get_flags);

/***
 * @method task:get_digest()
 * Returns message's unique digest (32 hex symbols)
 * @return {string} hex digest
 */
LUA_FUNCTION_DEF (task, get_digest);

/***
 * @method task:store_in_file([mode])
 * If task was loaded using file scan, then this method just returns its name,
 * otherwise, a fresh temporary file is created and its name is returned. Default
 * mode is 0600. To convert lua number to the octal mode you can use the following
 * trick: `tonumber("0644", 8)`. The file is automatically removed when task is
 * destroyed.
 *
 * @param {number} mode mode for new file
 * @return {string} file name with task content
 */
LUA_FUNCTION_DEF (task, store_in_file);

/***
 * @method task:get_protocol_reply([flags])
 * This method being called from a **postfilter** will return reply for a message
 * as it is returned to a client. This method returns the Lua table corresponding
 * to the UCL object. Flags is a table that specify which information should be
 * there in a reply:
 *
 * - `basic`: basic info, such as message-id
 * - `metrics`: metrics and symbols
 * - `messages`: messages
 * - `dkim`: dkim signature
 * - `rmilter`: rmilter control block
 * - `extra`: extra data, such as profiling
 * - `urls`: list of all urls in a message
 *
 * @param {table} flags table of flags (default is all flags but `urls`)
 * @return {table} ucl object corresponding to the reply
 */
LUA_FUNCTION_DEF (task, get_protocol_reply);

static const struct luaL_reg tasklib_f[] = {
	{NULL, NULL}
};

static const struct luaL_reg tasklib_m[] = {
	LUA_INTERFACE_DEF (task, get_message),
	LUA_INTERFACE_DEF (task, destroy),
	LUA_INTERFACE_DEF (task, process_message),
	LUA_INTERFACE_DEF (task, set_cfg),
	LUA_INTERFACE_DEF (task, get_cfg),
	LUA_INTERFACE_DEF (task, get_mempool),
	LUA_INTERFACE_DEF (task, get_session),
	LUA_INTERFACE_DEF (task, get_ev_base),
	LUA_INTERFACE_DEF (task, insert_result),
	LUA_INTERFACE_DEF (task, set_pre_result),
	LUA_INTERFACE_DEF (task, append_message),
	LUA_INTERFACE_DEF (task, has_urls),
	LUA_INTERFACE_DEF (task, get_urls),
	LUA_INTERFACE_DEF (task, get_content),
	LUA_INTERFACE_DEF (task, get_rawbody),
	LUA_INTERFACE_DEF (task, get_emails),
	LUA_INTERFACE_DEF (task, get_text_parts),
	LUA_INTERFACE_DEF (task, get_parts),
	LUA_INTERFACE_DEF (task, get_request_header),
	LUA_INTERFACE_DEF (task, set_request_header),
	LUA_INTERFACE_DEF (task, get_header),
	LUA_INTERFACE_DEF (task, get_header_raw),
	LUA_INTERFACE_DEF (task, get_header_full),
	LUA_INTERFACE_DEF (task, get_raw_headers),
	LUA_INTERFACE_DEF (task, get_received_headers),
	LUA_INTERFACE_DEF (task, get_queue_id),
	LUA_INTERFACE_DEF (task, get_uid),
	LUA_INTERFACE_DEF (task, get_resolver),
	LUA_INTERFACE_DEF (task, inc_dns_req),
	LUA_INTERFACE_DEF (task, get_dns_req),
	LUA_INTERFACE_DEF (task, has_recipients),
	LUA_INTERFACE_DEF (task, get_recipients),
	LUA_INTERFACE_DEF (task, set_recipients),
	LUA_INTERFACE_DEF (task, has_from),
	LUA_INTERFACE_DEF (task, get_from),
	LUA_INTERFACE_DEF (task, set_from),
	LUA_INTERFACE_DEF (task, get_user),
	LUA_INTERFACE_DEF (task, set_user),
	{"get_addr", lua_task_get_from_ip},
	{"get_ip", lua_task_get_from_ip},
	{"get_from_addr", lua_task_get_from_ip},
	LUA_INTERFACE_DEF (task, get_from_ip),
	LUA_INTERFACE_DEF (task, set_from_ip),
	LUA_INTERFACE_DEF (task, get_from_ip_num),
	LUA_INTERFACE_DEF (task, get_client_ip),
	LUA_INTERFACE_DEF (task, get_subject),
	LUA_INTERFACE_DEF (task, get_helo),
	LUA_INTERFACE_DEF (task, set_helo),
	LUA_INTERFACE_DEF (task, get_hostname),
	LUA_INTERFACE_DEF (task, set_hostname),
	LUA_INTERFACE_DEF (task, get_images),
	LUA_INTERFACE_DEF (task, get_archives),
	LUA_INTERFACE_DEF (task, get_symbol),
	LUA_INTERFACE_DEF (task, get_symbols),
	LUA_INTERFACE_DEF (task, get_symbols_all),
	LUA_INTERFACE_DEF (task, get_symbols_numeric),
	LUA_INTERFACE_DEF (task, has_symbol),
	LUA_INTERFACE_DEF (task, get_date),
	LUA_INTERFACE_DEF (task, get_message_id),
	LUA_INTERFACE_DEF (task, get_timeval),
	LUA_INTERFACE_DEF (task, get_metric_score),
	LUA_INTERFACE_DEF (task, get_metric_action),
	LUA_INTERFACE_DEF (task, set_metric_score),
	LUA_INTERFACE_DEF (task, set_metric_action),
	LUA_INTERFACE_DEF (task, set_metric_subject),
	LUA_INTERFACE_DEF (task, learn),
	LUA_INTERFACE_DEF (task, set_settings),
	LUA_INTERFACE_DEF (task, get_settings),
	LUA_INTERFACE_DEF (task, lookup_settings),
	LUA_INTERFACE_DEF (task, get_settings_id),
	LUA_INTERFACE_DEF (task, cache_get),
	LUA_INTERFACE_DEF (task, cache_set),
	LUA_INTERFACE_DEF (task, process_regexp),
	LUA_INTERFACE_DEF (task, get_size),
	LUA_INTERFACE_DEF (task, set_flag),
	LUA_INTERFACE_DEF (task, get_flags),
	LUA_INTERFACE_DEF (task, has_flag),
	LUA_INTERFACE_DEF (task, set_rmilter_reply),
	LUA_INTERFACE_DEF (task, get_digest),
	LUA_INTERFACE_DEF (task, store_in_file),
	LUA_INTERFACE_DEF (task, get_protocol_reply),
	{"__tostring", rspamd_lua_class_tostring},
	{NULL, NULL}
};

/* Image methods */
LUA_FUNCTION_DEF (image, get_width);
LUA_FUNCTION_DEF (image, get_height);
LUA_FUNCTION_DEF (image, get_type);
LUA_FUNCTION_DEF (image, get_filename);
LUA_FUNCTION_DEF (image, get_size);

static const struct luaL_reg imagelib_m[] = {
	LUA_INTERFACE_DEF (image, get_width),
	LUA_INTERFACE_DEF (image, get_height),
	LUA_INTERFACE_DEF (image, get_type),
	LUA_INTERFACE_DEF (image, get_filename),
	LUA_INTERFACE_DEF (image, get_size),
	{"__tostring", rspamd_lua_class_tostring},
	{NULL, NULL}
};

/* Archive methods */
LUA_FUNCTION_DEF (archive, get_type);
LUA_FUNCTION_DEF (archive, get_files);
LUA_FUNCTION_DEF (archive, get_files_full);
LUA_FUNCTION_DEF (archive, is_encrypted);
LUA_FUNCTION_DEF (archive, get_filename);
LUA_FUNCTION_DEF (archive, get_size);

static const struct luaL_reg archivelib_m[] = {
	LUA_INTERFACE_DEF (archive, get_type),
	LUA_INTERFACE_DEF (archive, get_files),
	LUA_INTERFACE_DEF (archive, get_files_full),
	LUA_INTERFACE_DEF (archive, is_encrypted),
	LUA_INTERFACE_DEF (archive, get_filename),
	LUA_INTERFACE_DEF (archive, get_size),
	{"__tostring", rspamd_lua_class_tostring},
	{NULL, NULL}
};

/* Blob methods */
LUA_FUNCTION_DEF (text, len);
LUA_FUNCTION_DEF (text, str);
LUA_FUNCTION_DEF (text, ptr);
LUA_FUNCTION_DEF (text, save_in_file);
LUA_FUNCTION_DEF (text, take_ownership);
LUA_FUNCTION_DEF (text, gc);

static const struct luaL_reg textlib_m[] = {
	LUA_INTERFACE_DEF (text, len),
	LUA_INTERFACE_DEF (text, str),
	LUA_INTERFACE_DEF (text, ptr),
	LUA_INTERFACE_DEF (text, take_ownership),
	LUA_INTERFACE_DEF (text, save_in_file),
	{"__len", lua_text_len},
	{"__tostring", lua_text_str},
	{"__gc", lua_text_gc},
	{NULL, NULL}
};

/* Utility functions */
struct rspamd_task *
lua_check_task (lua_State * L, gint pos)
{
	void *ud = rspamd_lua_check_udata (L, pos, "rspamd{task}");
	luaL_argcheck (L, ud != NULL, pos, "'task' expected");
	return ud ? *((struct rspamd_task **)ud) : NULL;
}

struct rspamd_task *
lua_check_task_maybe (lua_State * L, gint pos)
{
	void *ud = rspamd_lua_check_udata_maybe (L, pos, "rspamd{task}");

	return ud ? *((struct rspamd_task **)ud) : NULL;
}

static struct rspamd_image *
lua_check_image (lua_State * L)
{
	void *ud = rspamd_lua_check_udata (L, 1, "rspamd{image}");
	luaL_argcheck (L, ud != NULL, 1, "'image' expected");
	return ud ? *((struct rspamd_image **)ud) : NULL;
}

static struct rspamd_archive *
lua_check_archive (lua_State * L)
{
	void *ud = rspamd_lua_check_udata (L, 1, "rspamd{archive}");
	luaL_argcheck (L, ud != NULL, 1, "'archive' expected");
	return ud ? *((struct rspamd_archive **)ud) : NULL;
}

struct rspamd_lua_text *
lua_check_text (lua_State * L, gint pos)
{
	void *ud = rspamd_lua_check_udata (L, pos, "rspamd{text}");
	luaL_argcheck (L, ud != NULL, pos, "'text' expected");
	return ud ? (struct rspamd_lua_text *)ud : NULL;
}

static void
lua_task_set_cached (lua_State *L, struct rspamd_task *task, const gchar *key,
		gint pos)
{
	gpointer elt;
	gint lua_ref;

	lua_pushvalue (L, pos);

	elt = g_hash_table_lookup (task->lua_cache, key);

	if (G_UNLIKELY (elt != NULL)) {
		/* Unref previous value */
		lua_ref = GPOINTER_TO_INT (elt);
		luaL_unref (L, LUA_REGISTRYINDEX, lua_ref);
	}

	lua_ref = luaL_ref (L, LUA_REGISTRYINDEX);
	g_hash_table_insert (task->lua_cache, (void *)key, GINT_TO_POINTER (lua_ref));
}

static gboolean
lua_task_get_cached (lua_State *L, struct rspamd_task *task, const gchar *key)
{
	gpointer elt;

	elt = g_hash_table_lookup (task->lua_cache, key);

	if (elt != NULL) {
		lua_rawgeti (L, LUA_REGISTRYINDEX, GPOINTER_TO_INT (elt));

		return TRUE;
	}

	return FALSE;
}

/* Task methods */
static int
lua_task_process_message (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);

	if (task != NULL) {
		if (task->msg.len > 0) {
			if (rspamd_message_parse (task) == 0) {
				lua_pushboolean (L, TRUE);
			}
			else {
				lua_pushboolean (L, FALSE);
			}
		}
		else {
			lua_pushnil (L);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static int
lua_task_get_cfg (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	struct rspamd_config **pcfg;

	if (task) {
		pcfg = lua_newuserdata (L, sizeof (gpointer));
		rspamd_lua_setclass (L, "rspamd{config}", -1);
		*pcfg = task->cfg;
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static int
lua_task_set_cfg (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	void *ud = rspamd_lua_check_udata (L, 2, "rspamd{config}");

	if (task) {
		luaL_argcheck (L, ud != NULL, 1, "'config' expected");
		task->cfg = ud ? *((struct rspamd_config **)ud) : NULL;
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 0;
}

static int
lua_task_destroy (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);

	if (task != NULL) {
		rspamd_task_free (task);
	}

	return 0;
}

static int
lua_task_get_message (lua_State * L)
{
	return luaL_error (L, "task:get_message is no longer supported");
}

static int
lua_task_get_mempool (lua_State * L)
{
	rspamd_mempool_t **ppool;
	struct rspamd_task *task = lua_check_task (L, 1);

	if (task != NULL) {
		ppool = lua_newuserdata (L, sizeof (rspamd_mempool_t *));
		rspamd_lua_setclass (L, "rspamd{mempool}", -1);
		*ppool = task->task_pool;
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static int
lua_task_get_session (lua_State * L)
{
	struct rspamd_async_session **psession;
	struct rspamd_task *task = lua_check_task (L, 1);

	if (task != NULL) {
		psession = lua_newuserdata (L, sizeof (void *));
		rspamd_lua_setclass (L, "rspamd{session}", -1);
		*psession = task->s;
	}
	else {
		return luaL_error (L, "invalid arguments");
	}
	return 1;
}

static int
lua_task_get_ev_base (lua_State * L)
{
	struct event_base **pbase;
	struct rspamd_task *task = lua_check_task (L, 1);

	if (task != NULL) {
		pbase = lua_newuserdata (L, sizeof (struct event_base *));
		rspamd_lua_setclass (L, "rspamd{ev_base}", -1);
		*pbase = task->ev_base;
	}
	else {
		return luaL_error (L, "invalid arguments");
	}
	return 1;
}

static gint
lua_task_insert_result (lua_State * L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	const gchar *symbol_name, *param;
	double flag;
	struct rspamd_symbol_result *s;
	gint i, top;

	if (task != NULL) {
		symbol_name =
			rspamd_mempool_strdup (task->task_pool, luaL_checkstring (L, 2));
		flag = luaL_checknumber (L, 3);
		top = lua_gettop (L);
		s = rspamd_task_insert_result (task, symbol_name, flag, NULL);

		/* Get additional options */
		if (s) {
			for (i = 4; i <= top; i++) {
				if (lua_type (L, i) == LUA_TSTRING) {
					param = luaL_checkstring (L, i);
					rspamd_task_add_result_option (task, s, param);
				}
				else if (lua_type (L, i) == LUA_TTABLE) {
					lua_pushvalue (L, i);
					lua_pushnil (L);

					while (lua_next (L, -2)) {
						param = lua_tostring (L, -1);
						rspamd_task_add_result_option (task, s, param);
						lua_pop (L, 1);
					}

					lua_pop (L, 1);
				}
			}
		}

	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 0;
}

static gint
lua_task_set_pre_result (lua_State * L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	struct rspamd_metric_result *mres;
	gchar *action_str;
	gint action = METRIC_ACTION_MAX;

	if (task != NULL) {
		if (lua_type (L, 2) == LUA_TNUMBER) {
			action = lua_tointeger (L, 2);
		}
		else if (lua_type (L, 2) == LUA_TSTRING) {
			rspamd_action_from_str (lua_tostring (L, 2), &action);
		}

		if (action < METRIC_ACTION_MAX && action >= METRIC_ACTION_REJECT) {
			/* We also need to set the default metric to that result */
			mres = rspamd_create_metric_result (task, DEFAULT_METRIC);
			if (mres != NULL) {
				mres->score = mres->metric->actions[action].score;
				mres->action = action;
			}

			task->pre_result.action = action;

			if (lua_gettop (L) >= 3) {
				action_str = rspamd_mempool_strdup (task->task_pool,
						luaL_checkstring (L, 3));
				task->pre_result.str = action_str;
				ucl_object_insert_key (task->messages,
						ucl_object_fromstring (action_str), "smtp_message", 0,
						false);
			}
			else {
				task->pre_result.str = "unknown";
			}
			msg_info_task ("<%s>: set pre-result to %s: '%s'",
						task->message_id, rspamd_action_to_str (action),
						task->pre_result.str);

			/* Don't classify or filter message if pre-filter sets results */
			task->processed_stages |= (RSPAMD_TASK_STAGE_FILTERS |
					RSPAMD_TASK_STAGE_CLASSIFIERS |
					RSPAMD_TASK_STAGE_CLASSIFIERS_PRE |
					RSPAMD_TASK_STAGE_CLASSIFIERS_POST);
		}
		else {
			return luaL_error (L, "invalid arguments");
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 0;
}

static gint
lua_task_append_message (lua_State * L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	const gchar *message = luaL_checkstring (L, 2), *category;

	if (task != NULL) {
		if (lua_type (L, 3) == LUA_TSTRING) {
			category = luaL_checkstring (L, 3);
		}
		else {
			category = "unknown";
		}

		ucl_object_insert_key (task->messages,
				ucl_object_fromstring (message), category, 0,
				true);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 0;
}

struct lua_tree_cb_data {
	lua_State *L;
	int i;
};

static void
lua_tree_url_callback (gpointer key, gpointer value, gpointer ud)
{
	struct rspamd_lua_url *url;
	struct lua_tree_cb_data *cb = ud;

	url = lua_newuserdata (cb->L, sizeof (struct rspamd_lua_url));
	rspamd_lua_setclass (cb->L, "rspamd{url}", -1);
	url->url = value;
	lua_rawseti (cb->L, -2, cb->i++);
}

static gint
lua_task_get_urls (lua_State * L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	struct lua_tree_cb_data cb;
	gboolean need_emails = FALSE;
	gsize sz;

	if (task) {
		if (lua_gettop (L) >= 2) {
			need_emails = lua_toboolean (L, 2);
		}

		if (need_emails) {
			if (!lua_task_get_cached (L, task, "emails+urls")) {
				sz = g_hash_table_size (task->urls);
				sz += g_hash_table_size (task->emails);

				lua_createtable (L, sz, 0);
				cb.i = 1;
				cb.L = L;
				g_hash_table_foreach (task->urls, lua_tree_url_callback, &cb);
				g_hash_table_foreach (task->emails, lua_tree_url_callback, &cb);

				lua_task_set_cached (L, task, "emails+urls", -1);
			}

		}
		else {
			if (!lua_task_get_cached (L, task, "urls")) {
				sz = g_hash_table_size (task->urls);

				lua_createtable (L, sz, 0);
				cb.i = 1;
				cb.L = L;
				g_hash_table_foreach (task->urls, lua_tree_url_callback, &cb);

				lua_task_set_cached (L, task, "urls", -1);
			}
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_has_urls (lua_State * L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	gboolean need_emails = FALSE, ret = FALSE;

	if (task) {
		if (lua_gettop (L) >= 2) {
			need_emails = lua_toboolean (L, 2);
		}

		if (g_hash_table_size (task->urls) > 0) {
			ret = TRUE;
		}

		if (need_emails && g_hash_table_size (task->emails) > 0) {
			ret = TRUE;
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	lua_pushboolean (L, ret);

	return 1;
}

static gint
lua_task_get_content (lua_State * L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	struct rspamd_lua_text *t;

	if (task) {
		t = lua_newuserdata (L, sizeof (*t));
		rspamd_lua_setclass (L, "rspamd{text}", -1);
		t->len = task->msg.len;
		t->start = task->msg.begin;
		t->flags = 0;
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_get_rawbody (lua_State * L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	struct rspamd_lua_text *t;

	if (task) {
		t = lua_newuserdata (L, sizeof (*t));
		rspamd_lua_setclass (L, "rspamd{text}", -1);

		if (task->raw_headers_content.len > 0) {
			g_assert (task->raw_headers_content.len <= task->msg.len);
			t->start = task->msg.begin + task->raw_headers_content.len;
			t->len = task->msg.len - task->raw_headers_content.len;
		}
		else {
			t->len = task->msg.len;
			t->start = task->msg.begin;
		}

		t->flags = 0;
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_get_emails (lua_State * L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	struct lua_tree_cb_data cb;

	if (task) {
		lua_createtable (L, g_hash_table_size (task->emails), 0);
		cb.i = 1;
		cb.L = L;
		g_hash_table_foreach (task->emails, lua_tree_url_callback, &cb);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_get_text_parts (lua_State * L)
{
	guint i;
	struct rspamd_task *task = lua_check_task (L, 1);
	struct rspamd_mime_text_part *part, **ppart;

	if (task != NULL) {

		if (!lua_task_get_cached (L, task, "text_parts")) {
			lua_createtable (L, task->text_parts->len, 0);

			for (i = 0; i < task->text_parts->len; i ++) {
				part = g_ptr_array_index (task->text_parts, i);
				ppart = lua_newuserdata (L, sizeof (struct rspamd_mime_text_part *));
				*ppart = part;
				rspamd_lua_setclass (L, "rspamd{textpart}", -1);
				/* Make it array */
				lua_rawseti (L, -2, i + 1);
			}

			lua_task_set_cached (L, task, "text_parts", -1);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_get_parts (lua_State * L)
{
	guint i;
	struct rspamd_task *task = lua_check_task (L, 1);
	struct rspamd_mime_part *part, **ppart;

	if (task != NULL) {
		if (!lua_task_get_cached (L, task, "mime_parts")) {
			lua_createtable (L, task->parts->len, 0);

			for (i = 0; i < task->parts->len; i ++) {
				part = g_ptr_array_index (task->parts, i);
				ppart = lua_newuserdata (L, sizeof (struct rspamd_mime_part *));
				*ppart = part;
				rspamd_lua_setclass (L, "rspamd{mimepart}", -1);
				/* Make it array */
				lua_rawseti (L, -2, i + 1);
			}

			lua_task_set_cached (L, task, "mime_parts", -1);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_get_request_header (lua_State *L)
{
	rspamd_ftok_t *hdr;
	struct rspamd_task *task = lua_check_task (L, 1);
	const gchar *s;
	struct rspamd_lua_text *t;

	s = luaL_checkstring (L, 2);

	if (s && task) {
		hdr = rspamd_task_get_request_header (task, s);

		if (hdr) {
			t = lua_newuserdata (L, sizeof (*t));
			rspamd_lua_setclass (L, "rspamd{text}", -1);
			t->start = hdr->begin;
			t->len = hdr->len;
			t->flags = 0;

			return 1;
		}
		else {
			lua_pushnil (L);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_set_request_header (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	const gchar *s, *v = NULL;
	rspamd_fstring_t *buf;
	struct rspamd_lua_text *t;
	rspamd_ftok_t *hdr, *new_name;
	gsize len, vlen;

	s = luaL_checklstring (L, 2, &len);

	if (s && task) {
		if (lua_type (L, 3) == LUA_TSTRING) {
			v = luaL_checklstring (L, 2, &vlen);
		}
		else if (lua_type (L, 3) == LUA_TUSERDATA) {
			t = lua_check_text (L, 3);

			if (t != NULL) {
				v = t->start;
				vlen = t->len;
			}
		}

		if (v != NULL) {
			buf = rspamd_fstring_new_init (v, vlen);
			hdr = rspamd_ftok_map (buf);
			buf = rspamd_fstring_new_init (s, len);
			new_name = rspamd_ftok_map (buf);

			rspamd_task_add_request_header (task, new_name, hdr);
		}

	}
	else {
		return luaL_error (L, "invalid arguments");
	}


	return 0;
}

gint
rspamd_lua_push_header (lua_State * L,
		GPtrArray *ar,
		const gchar *name,
		gboolean strong,
		gboolean full,
		gboolean raw)
{

	struct rspamd_mime_header *rh;
	guint i;
	const gchar *val;

	if (ar == NULL || ar->len == 0) {
		lua_pushnil (L);
		return 1;
	}

	if (full) {
		lua_createtable (L, ar->len, 0);
	}

	PTR_ARRAY_FOREACH (ar, i, rh) {
		if (full) {
			/* Create new associated table for a header */
			lua_createtable (L, 0, 7);
			rspamd_lua_table_set (L, "name",	 rh->name);

			if (rh->value) {
				rspamd_lua_table_set (L, "value", rh->value);
			}

			if (rh->decoded) {
				rspamd_lua_table_set (L, "decoded", rh->decoded);
			}

			lua_pushstring (L, "tab_separated");
			lua_pushboolean (L, rh->tab_separated);
			lua_settable (L, -3);
			lua_pushstring (L, "empty_separator");
			lua_pushboolean (L, rh->empty_separator);
			lua_settable (L, -3);
			rspamd_lua_table_set (L, "separator", rh->separator);
			lua_pushstring (L, "order");
			lua_pushnumber (L, rh->order);
			lua_settable (L, -3);
			lua_rawseti (L, -2, i + 1);
		}
		else {
			if (!raw) {
				val = rh->decoded;
			}
			else {
				val = rh->value;
			}

			if (val) {
				lua_pushstring (L, val);
			}
			else {
				lua_pushnil (L);
			}

			return 1;
		}
	}

	return 1;
}

static gint
lua_task_get_header_common (lua_State *L, gboolean full, gboolean raw)
{
	gboolean strong = FALSE;
	struct rspamd_task *task = lua_check_task (L, 1);
	const gchar *name;
	GPtrArray *ar;

	name = luaL_checkstring (L, 2);

	if (name && task) {
		if (lua_gettop (L) == 3) {
			strong = lua_toboolean (L, 3);
		}

		ar = rspamd_message_get_header_array (task, name, strong);

		return rspamd_lua_push_header (L, ar, name,
				strong, full, raw);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}
}

static gint
lua_task_get_header_full (lua_State * L)
{
	return lua_task_get_header_common (L, TRUE, TRUE);
}

static gint
lua_task_get_header (lua_State * L)
{
	return lua_task_get_header_common (L, FALSE, FALSE);
}

static gint
lua_task_get_header_raw (lua_State * L)
{
	return lua_task_get_header_common (L, FALSE, TRUE);
}

static gint
lua_task_get_raw_headers (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	struct rspamd_lua_text *t;

	if (task) {
		t = lua_newuserdata (L, sizeof (*t));
		rspamd_lua_setclass (L, "rspamd{text}", -1);
		t->start = task->raw_headers_content.begin;
		t->len = task->raw_headers_content.len;
		t->flags = 0;
	}
	else {
		return luaL_error (L, "invalid arguments");
	}


	return 1;
}

static gint
lua_task_get_received_headers (lua_State * L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	struct received_header *rh;
	const gchar *proto;
	guint i, k = 1;

	if (task) {
		if (!lua_task_get_cached (L, task, "received")) {
			lua_createtable (L, task->received->len, 0);

			for (i = 0; i < task->received->len; i ++) {
				rh = g_ptr_array_index (task->received, i);

				lua_createtable (L, 0, 10);

				if (rh->hdr && rh->hdr->decoded) {
					rspamd_lua_table_set (L, "raw", rh->hdr->decoded);
				}

				lua_pushstring (L, "flags");
				lua_createtable (L, 0, 3);

				lua_pushstring (L, "artificial");
				if (rh->flags & RSPAMD_RECEIVED_FLAG_ARTIFICIAL) {
					lua_pushboolean (L, true);
				}
				else {
					lua_pushboolean (L, false);
				}
				lua_settable (L, -3);

				lua_pushstring (L, "authenticated");
				if (rh->flags & RSPAMD_RECEIVED_FLAG_AUTHENTICATED) {
					lua_pushboolean (L, true);
				}
				else {
					lua_pushboolean (L, false);
				}
				lua_settable (L, -3);

				lua_pushstring (L, "ssl");
				if (rh->flags & RSPAMD_RECEIVED_FLAG_SSL) {
					lua_pushboolean (L, true);
				}
				else {
					lua_pushboolean (L, false);
				}
				lua_settable (L, -3);

				lua_settable (L, -3);

				if (G_UNLIKELY (rh->from_ip == NULL &&
						rh->real_ip == NULL &&
						rh->real_hostname == NULL &&
						rh->by_hostname == NULL && rh->timestamp == 0 &&
						rh->for_mbox == NULL)) {
					lua_rawseti (L, -2, k ++);

					continue;
				}

				rspamd_lua_table_set (L, "from_hostname", rh->from_hostname);
				rspamd_lua_table_set (L, "from_ip", rh->from_ip);
				rspamd_lua_table_set (L, "real_hostname", rh->real_hostname);
				lua_pushstring (L, "real_ip");
				rspamd_lua_ip_push (L, rh->addr);
				lua_settable (L, -3);
				lua_pushstring (L, "proto");

				switch (rh->type) {
				case RSPAMD_RECEIVED_SMTP:
					proto = "smtp";
					break;
				case RSPAMD_RECEIVED_ESMTP:
					proto = "esmtp";
					break;
				case RSPAMD_RECEIVED_ESMTPS:
					proto = "esmtps";
					break;
				case RSPAMD_RECEIVED_ESMTPA:
					proto = "esmtpa";
					break;
				case RSPAMD_RECEIVED_ESMTPSA:
					proto = "esmtpsa";
					break;
				case RSPAMD_RECEIVED_LMTP:
					proto = "lmtp";
					break;
				case RSPAMD_RECEIVED_IMAP:
					proto = "imap";
					break;
				case RSPAMD_RECEIVED_UNKNOWN:
				default:
					proto = "unknown";
					break;
				}

				lua_pushstring (L, proto);
				lua_settable (L, -3);

				lua_pushstring (L, "timestamp");
				lua_pushnumber (L, rh->timestamp);
				lua_settable (L, -3);

				rspamd_lua_table_set (L, "by_hostname", rh->by_hostname);
				rspamd_lua_table_set (L, "for", rh->for_mbox);
				lua_rawseti (L, -2, k ++);
			}

			lua_task_set_cached (L, task, "received", -1);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_get_queue_id (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);

	if (task) {
		if (task->queue_id != NULL && strcmp (task->queue_id, "undef") != 0) {
			lua_pushstring (L, task->queue_id);
		}
		else {
			lua_pushnil (L);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_get_uid (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);

	if (task) {
		lua_pushstring (L, task->task_pool->tag.uid);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_get_resolver (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	struct rspamd_dns_resolver **presolver;

	if (task != NULL && task->resolver != NULL) {
		presolver = lua_newuserdata (L, sizeof (void *));
		rspamd_lua_setclass (L, "rspamd{resolver}", -1);
		*presolver = task->resolver;
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_inc_dns_req (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);

	if (task != NULL) {
		task->dns_requests++;
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 0;
}

static gint
lua_task_get_dns_req (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);

	if (task != NULL) {
		lua_pushnumber (L, task->dns_requests);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

enum rspamd_address_type {
	RSPAMD_ADDRESS_ANY = 0,
	RSPAMD_ADDRESS_SMTP = 1,
	RSPAMD_ADDRESS_MIME = 2,
	RSPAMD_ADDRESS_RAW_ANY = 3,
	RSPAMD_ADDRESS_RAW_SMTP = 4,
	RSPAMD_ADDRESS_RAW_MIME = 5,
	RSPAMD_ADDRESS_MAX
};

/*
 * Convert element at the specified position to the type
 * for get_from/get_recipients
 */
static enum rspamd_address_type
lua_task_str_to_get_type (lua_State *L, gint pos)
{
	const gchar *type = NULL;
	gint ret = RSPAMD_ADDRESS_ANY;
	guint64 h;
	gsize sz;

	/* Get what value */

	if (lua_type (L, pos) == LUA_TNUMBER) {
		ret = lua_tonumber (L, pos);

		if (ret >= RSPAMD_ADDRESS_ANY && ret < RSPAMD_ADDRESS_MAX) {
			return ret;
		}

		return RSPAMD_ADDRESS_ANY;
	}
	else if (lua_type (L, pos) == LUA_TSTRING) {
		type = lua_tolstring (L, pos, &sz);

		if (type && sz > 0) {
			h = rspamd_cryptobox_fast_hash_specific (RSPAMD_CRYPTOBOX_XXHASH64,
					type, sz, 0xdeadbabe);

			switch (h) {
			case 0xDA081341FB600389ULL: /* mime */
				ret = RSPAMD_ADDRESS_MIME;
				break;
			case 0xEEC8A7832F8C43ACULL: /* any */
				ret = RSPAMD_ADDRESS_ANY;
				break;
			case 0x472274D5193B2A80ULL: /* smtp */
			case 0xEFE0F586CC9F14A9ULL: /* envelope */
				ret = RSPAMD_ADDRESS_SMTP;
				break;
			case 0x9DA887501690DE20ULL: /* raw_mime */
				ret = RSPAMD_ADDRESS_RAW_MIME;
				break;
			case 0x6B54FE02DEB595A4ULL: /* raw_smtp */
			case 0xE0E596C861777B02ULL: /* raw_envelope */
				ret = RSPAMD_ADDRESS_RAW_SMTP;
				break;
			case 0x2C49DBE3A10A0197ULL: /* raw_any */
				ret = RSPAMD_ADDRESS_RAW_ANY;
				break;
			}
		}
	}

	return ret;
}

static void
lua_push_email_address (lua_State *L, struct rspamd_email_address *addr)
{
	if (addr) {
		lua_createtable (L, 0, 4);

		if (addr->addr_len > 0) {
			lua_pushstring (L, "addr");
			lua_pushlstring (L, addr->addr, addr->addr_len);
			lua_settable (L, -3);
		}
		else {
			lua_pushstring (L, "addr");
			lua_pushstring (L, "");
			lua_settable (L, -3);
		}
		if (addr->domain_len > 0) {
			lua_pushstring (L, "domain");
			lua_pushlstring (L, addr->domain, addr->domain_len);
			lua_settable (L, -3);
		}
		else {
			lua_pushstring (L, "domain");
			lua_pushstring (L, "");
			lua_settable (L, -3);
		}
		if (addr->user_len > 0) {
			lua_pushstring (L, "user");
			lua_pushlstring (L, addr->user, addr->user_len);
			lua_settable (L, -3);
		}
		else {
			lua_pushstring (L, "user");
			lua_pushstring (L, "");
			lua_settable (L, -3);
		}

		if (addr->name_len > 0) {
			lua_pushstring (L, "name");
			lua_pushlstring (L, addr->name, addr->name_len);
			lua_settable (L, -3);
		}
		else {
			lua_pushstring (L, "name");
			lua_pushstring (L, "");
			lua_settable (L, -3);
		}
	}
}

void
lua_push_emails_address_list (lua_State *L, GPtrArray *addrs)
{
	struct rspamd_email_address *addr;
	guint i;

	lua_createtable (L, addrs->len, 0);

	for (i = 0; i < addrs->len; i ++) {
		addr = g_ptr_array_index (addrs, i);
		lua_push_email_address (L, addr);
		lua_rawseti (L, -2, i + 1);
	}
}

static gboolean
lua_import_email_address (lua_State *L, struct rspamd_task *task,
		gint pos,
		struct rspamd_email_address **paddr)
{
	struct rspamd_email_address *addr;
	const gchar *p;
	gsize len;

	g_assert (paddr != NULL);

	if (!lua_istable (L, pos)) {
		return FALSE;
	}

	addr = rspamd_mempool_alloc0 (task->task_pool, sizeof (*addr));

	lua_pushstring (L, "name");
	lua_gettable (L, pos);

	if (lua_type (L, -1) == LUA_TSTRING) {
		p = lua_tolstring (L, -1, &len);
		addr->name = (const gchar *)rspamd_mempool_alloc (task->task_pool, len);
		memcpy ((gchar *)addr->name, p, len);
		addr->addr_len = len;
	}

	lua_pop (L, 1);

	lua_pushstring (L, "user");
	lua_gettable (L, pos);

	if (lua_type (L, -1) == LUA_TSTRING) {
		p = lua_tolstring (L, -1, &len);
		addr->user = (const gchar *)rspamd_mempool_alloc (task->task_pool, len);
		memcpy ((gchar *)addr->user, p, len);
		addr->user_len = len;
	}

	lua_pop (L, 1);

	lua_pushstring (L, "domain");
	lua_gettable (L, pos);

	if (lua_type (L, -1) == LUA_TSTRING) {
		p = lua_tolstring (L, -1, &len);
		addr->domain = (const gchar *)rspamd_mempool_alloc (task->task_pool, len);
		memcpy ((gchar *)addr->domain, p, len);
		addr->domain_len = len;
	}

	lua_pop (L, 1);

	lua_pushstring (L, "addr");
	lua_gettable (L, pos);

	if (lua_type (L, -1) == LUA_TSTRING) {
		p = lua_tolstring (L, -1, &len);
		addr->addr = (const gchar *)rspamd_mempool_alloc (task->task_pool, len);
		memcpy ((gchar *)addr->addr, p, len);
		addr->addr_len = len;
	}
	else {
		/* Construct addr */
		len = addr->domain_len + addr->user_len + 1;
		addr->addr = (const gchar *)rspamd_mempool_alloc (task->task_pool, len);
		addr->addr_len = rspamd_snprintf ((gchar *)addr->addr, len, "%*s@%*s",
					(int)addr->user_len, addr->user,
					(int)addr->domain_len, addr->domain);
	}

	lua_pop (L, 1);

	lua_pushstring (L, "raw");
	lua_gettable (L, pos);

	if (lua_type (L, -1) == LUA_TSTRING) {
		p = lua_tolstring (L, -1, &len);
		addr->raw = (const gchar *)rspamd_mempool_alloc (task->task_pool, len);
		memcpy ((gchar *)addr->raw, p, len);
		addr->raw_len = len;
	}
	else {
		/* Construct raw addr */
		len = addr->addr_len + addr->name_len + 3;
		addr->raw = (const gchar *)rspamd_mempool_alloc (task->task_pool, len);
		if (addr->name_len > 0) {
			addr->raw_len = rspamd_snprintf ((gchar *)addr->raw, len, "%*s <%*s>",
					(int)addr->name_len, addr->name,
					(int)addr->addr_len, addr->addr);

		}
		else {
			addr->raw_len = rspamd_snprintf ((gchar *)addr->raw, len, "<%*s@%*s>",
					(int)addr->name_len, addr->name,
					(int)addr->addr_len, addr->addr);
		}
	}

	lua_pop (L, 1);

	*paddr = addr;

	return TRUE;
}

static gint
lua_task_get_recipients (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	GPtrArray *ptrs = NULL;
	gint what = 0;

	if (task) {
		if (lua_gettop (L) == 2) {
			/* Get what value */
			what = lua_task_str_to_get_type (L, 2);
		}

		switch (what) {
		case RSPAMD_ADDRESS_SMTP:
			/* Here we check merely envelope rcpt */
			ptrs = task->rcpt_envelope;
			break;
		case RSPAMD_ADDRESS_MIME:
			/* Here we check merely mime rcpt */
			ptrs = task->rcpt_mime;
			break;
		case RSPAMD_ADDRESS_ANY:
		default:
			if (task->rcpt_envelope) {
				ptrs = task->rcpt_envelope;
			}
			else {
				ptrs = task->rcpt_mime;
			}
			break;
		}
		if (ptrs) {
			lua_push_emails_address_list (L, ptrs);
		}
		else {
			lua_pushnil (L);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_set_recipients (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	GPtrArray *ptrs = NULL;
	struct rspamd_email_address *addr = NULL;
	gint what = 0, pos = 3;

	if (task) {
		if (lua_isstring (L, 2) || lua_isnumber (L, 2)) {
			/* Get what value */
			what = lua_task_str_to_get_type (L, 2);
		}
		else if (lua_istable (L, 2)) {
			pos = 2;
		}

		switch (what) {
		case RSPAMD_ADDRESS_SMTP:
			/* Here we check merely envelope rcpt */
			ptrs = task->rcpt_envelope;
			break;
		case RSPAMD_ADDRESS_MIME:
			/* Here we check merely mime rcpt */
			ptrs = task->rcpt_mime;
			break;
		case RSPAMD_ADDRESS_ANY:
		default:
			if (task->rcpt_envelope) {
				ptrs = task->rcpt_envelope;
			}
			else {
				ptrs = task->rcpt_mime;
			}
			break;
		}
		if (ptrs) {
			lua_pushvalue (L, pos);
			g_ptr_array_set_size (ptrs, 0);

			for (lua_pushnil (L); lua_next (L, -2); lua_pop (L, 1)) {
				if (lua_import_email_address (L, task, lua_gettop (L), &addr)) {
					g_ptr_array_add (ptrs, addr);
					addr = NULL;
				}
			}

			lua_pop (L, 1);
			lua_pushboolean (L, true);
		}
		else {
			lua_pushboolean (L, false);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}


#define CHECK_EMAIL_ADDR(addr) do { \
	if (addr == NULL) { \
		ret = 0; \
	} \
	else { \
		ret = addr->flags & RSPAMD_EMAIL_ADDR_VALID; \
	} \
} while (0)

#define CHECK_EMAIL_ADDR_LIST(addr) do { \
	if (addr == NULL) { \
		ret = 0; \
	} \
	else { \
		ret = addr->len > 0; \
	} \
} while (0)

static gint
lua_task_has_from (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	gint what = 0;
	gboolean ret = FALSE;

	if (task) {
		if (lua_gettop (L) == 2) {
			/* Get what value */
			what = lua_task_str_to_get_type (L, 2);
		}

		switch (what) {
		case RSPAMD_ADDRESS_SMTP:
			/* Here we check merely envelope rcpt */
			CHECK_EMAIL_ADDR (task->from_envelope);
			break;
		case RSPAMD_ADDRESS_MIME:
			/* Here we check merely mime rcpt */
			CHECK_EMAIL_ADDR_LIST (task->from_mime);
			break;
		case RSPAMD_ADDRESS_ANY:
		default:
			CHECK_EMAIL_ADDR (task->from_envelope);

			if (!ret) {
				CHECK_EMAIL_ADDR_LIST (task->from_mime);
			}
			break;
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	lua_pushboolean (L, ret);

	return 1;
}

static gint
lua_task_has_recipients (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	gint what = 0;
	gboolean ret = FALSE;

	if (task) {
		if (lua_gettop (L) == 2) {
			/* Get what value */
			what = lua_task_str_to_get_type (L, 2);
		}

		switch (what) {
		case RSPAMD_ADDRESS_SMTP:
			/* Here we check merely envelope rcpt */
			CHECK_EMAIL_ADDR_LIST (task->rcpt_envelope);
			break;
		case RSPAMD_ADDRESS_MIME:
			/* Here we check merely mime rcpt */
			CHECK_EMAIL_ADDR_LIST (task->rcpt_mime);
			break;
		case RSPAMD_ADDRESS_ANY:
		default:
			CHECK_EMAIL_ADDR_LIST (task->rcpt_envelope);

			if (!ret) {
				CHECK_EMAIL_ADDR_LIST (task->rcpt_mime);
			}
			break;
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	lua_pushboolean (L, ret);

	return 1;
}

static gint
lua_task_get_from (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	GPtrArray *addrs = NULL;
	struct rspamd_email_address *addr = NULL;
	gint what = 0;

	if (task) {
		if (lua_gettop (L) == 2) {
			/* Get what value */
			what = lua_task_str_to_get_type (L, 2);
		}

		switch (what) {
		case RSPAMD_ADDRESS_SMTP:
			/* Here we check merely envelope rcpt */
			addr = task->from_envelope;
			break;
		case RSPAMD_ADDRESS_MIME:
			/* Here we check merely mime rcpt */
			addrs = task->from_mime;
			break;
		case RSPAMD_ADDRESS_ANY:
		default:
			if (task->from_envelope) {
				addr = task->from_envelope;
			}
			else {
				addrs = task->from_mime;
			}
			break;
		}

		if (addrs) {
			lua_push_emails_address_list (L, addrs);
		}
		else if (addr) {
			/* Create table to preserve compatibility */
			if (addr->addr) {
				lua_createtable (L, 1, 0);
				lua_push_email_address (L, addr);
				lua_rawseti (L, -2, 1);
			}
			else {
				lua_pushnil (L);
			}
		}
		else {
			lua_pushnil (L);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_set_from (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	GPtrArray *addrs = NULL;
	struct rspamd_email_address **paddr = NULL, *addr;
	gint what = 0, pos = 3;

	if (task) {
		if (lua_isstring (L, 2) || lua_isnumber (L, 2)) {
			/* Get what value */
			what = lua_task_str_to_get_type (L, 2);
		}
		else if (lua_istable (L, 2)) {
			pos = 2;
		}

		switch (what) {
		case RSPAMD_ADDRESS_SMTP:
			/* Here we check merely envelope rcpt */
			paddr = &task->from_envelope;
			break;
		case RSPAMD_ADDRESS_MIME:
			/* Here we check merely mime rcpt */
			addrs = task->from_mime;
			break;
		case RSPAMD_ADDRESS_ANY:
		default:
			if (task->from_envelope) {
				paddr = &task->from_envelope;
			}
			else {
				addrs = task->from_mime;
			}
			break;
		}

		if (addrs) {
			if (lua_import_email_address (L, task, pos, &addr)) {
				g_ptr_array_set_size (addrs, 0);
				g_ptr_array_add (addrs, addr);
				lua_pushboolean (L, true);
			}
			else {
				lua_pushboolean (L, false);
			}
		}
		else if (paddr) {
			if (lua_import_email_address (L, task, pos, paddr)) {
				lua_pushboolean (L, true);
			}
			else {
				lua_pushboolean (L, false);
			}
		}
		else {
			lua_pushboolean (L, false);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_get_user (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);

	if (task) {
		if (task->user != NULL) {
			lua_pushstring (L, task->user);
		}
		else {
			lua_pushnil (L);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_set_user (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	const gchar *new_user;

	if (task) {
		new_user = luaL_checkstring (L, 2);
		if (new_user) {
			task->user = rspamd_mempool_strdup (task->task_pool, new_user);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 0;
}

static gint
lua_task_get_from_ip (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);

	if (task) {
		rspamd_lua_ip_push (L, task->from_addr);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_set_from_ip (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	const gchar *ip_str = luaL_checkstring (L, 2);
	rspamd_inet_addr_t *addr = NULL;

	if (!task || !ip_str) {
		lua_pushstring (L, "invalid parameters");
		return lua_error (L);
	}
	else {
		if (!rspamd_parse_inet_address (&addr,
				ip_str,
				0)) {
			msg_warn_task ("cannot get IP from received header: '%s'",
					ip_str);
		}
		else {
			if (task->from_addr) {
				rspamd_inet_address_destroy (task->from_addr);
			}

			task->from_addr = addr;
		}
	}

	return 0;
}

static gint
lua_task_get_from_ip_num (lua_State *L)
{
	msg_err ("this function is deprecated and should no longer be used");
	lua_pushnil (L);
	return 1;
}

static gint
lua_task_get_client_ip (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);

	if (task) {
		rspamd_lua_ip_push (L, task->client_addr);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_get_helo (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);

	if (task) {
		if (task->helo != NULL) {
			lua_pushstring (L, task->helo);
			return 1;
		}
		else {
			lua_pushnil (L);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_get_subject (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);

	if (task) {
		if (task->subject != NULL) {
			lua_pushstring (L, task->subject);
			return 1;
		}
		else {
			lua_pushnil (L);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_set_helo (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	const gchar *new_helo;

	if (task) {
		new_helo = luaL_checkstring (L, 2);
		if (new_helo) {
			task->helo = rspamd_mempool_strdup (task->task_pool, new_helo);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 0;
}

static gint
lua_task_get_hostname (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);

	if (task) {
		if (task->hostname != NULL) {
			/* Check whether it looks like an IP address */
			if (*task->hostname == '[') {
				/*
				 * From the milter documentation:
				 *  If the reverse lookup fails or if none of the IP
				 *  addresses of the resolved host name matches the
				 *  original IP address, hostname will contain the
				 *  message sender's IP address enclosed in square
				 *  brackets (e.g. `[a.b.c.d]')
				 */
				lua_pushstring (L, "unknown");
			}
			else {
				lua_pushstring (L, task->hostname);
			}
		}
		else {
			lua_pushnil (L);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_set_hostname (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	const gchar *new_hostname;

	if (task) {
		new_hostname = luaL_checkstring (L, 2);
		if (new_hostname) {
			task->hostname = rspamd_mempool_strdup (task->task_pool,
					new_hostname);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 0;
}

static gint
lua_task_get_images (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	guint nelt = 0, i;
	struct rspamd_mime_part *part;
	struct rspamd_image **pimg;

	if (task) {
		if (!lua_task_get_cached (L, task, "images")) {
			lua_createtable (L, task->parts->len, 0);

			for (i = 0; i < task->parts->len; i ++) {
				part = g_ptr_array_index (task->parts, i);

				if (part->flags & RSPAMD_MIME_PART_IMAGE) {
					pimg = lua_newuserdata (L, sizeof (struct rspamd_image *));
					rspamd_lua_setclass (L, "rspamd{image}", -1);
					*pimg = part->specific.img;
					lua_rawseti (L, -2, ++nelt);
				}
			}

			lua_task_set_cached (L, task, "images", -1);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_get_archives (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	guint nelt = 0, i;
	struct rspamd_mime_part *part;
	struct rspamd_archive **parch;

	if (task) {
		if (!lua_task_get_cached (L, task, "archives")) {
			lua_createtable (L, task->parts->len, 0);

			for (i = 0; i < task->parts->len; i ++) {
				part = g_ptr_array_index (task->parts, i);

				if (part->flags & RSPAMD_MIME_PART_ARCHIVE) {
					parch = lua_newuserdata (L, sizeof (struct rspamd_archive *));
					rspamd_lua_setclass (L, "rspamd{archive}", -1);
					*parch = part->specific.arch;
					lua_rawseti (L, -2, ++nelt);
				}
			}

			lua_task_set_cached (L, task, "archives", -1);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static inline gboolean
lua_push_symbol_result (lua_State *L,
		struct rspamd_task *task,
		struct rspamd_metric *metric,
		const gchar *symbol,
		struct rspamd_symbol_result *symbol_result,
		gboolean add_metric,
		gboolean add_name)
{
	struct rspamd_metric_result *metric_res;
	struct rspamd_symbol_result *s = NULL;
	struct rspamd_symbol_option *opt;
	gint j = 1, e = 4;

	if (!symbol_result) {
		metric_res = g_hash_table_lookup (task->results, metric->name);
		if (metric_res) {
			s = g_hash_table_lookup (metric_res->symbols, symbol);
		}
	}
	else {
		s = symbol_result;
	}

	if (s) {
		if (add_metric) {
			e++;
		}
		if (add_name) {
			e++;
		}

		lua_createtable (L, 0, e);

		if (add_metric) {
			lua_pushstring (L, "metric");
			lua_pushstring (L, metric->name);
			lua_settable (L, -3);
		}
		if (add_name) {
			lua_pushstring (L, "name");
			lua_pushstring (L, symbol);
			lua_settable (L, -3);
		}
		lua_pushstring (L, "score");
		lua_pushnumber (L, s->score);
		lua_settable (L, -3);

		if (s->sym && s->sym->gr) {
			lua_pushstring (L, "group");
			lua_pushstring (L, s->sym->gr->name);
			lua_settable (L, -3);
		}
		else {
			lua_pushstring (L, "group");
			lua_pushstring (L, "ungrouped");
			lua_settable (L, -3);
		}

		if (s->options) {
			lua_pushstring (L, "options");
			lua_createtable (L, g_hash_table_size (s->options), 0);

			DL_FOREACH (s->opts_head, opt) {
				lua_pushstring (L, (const char*)opt->option);
				lua_rawseti (L, -2, j++);
			}

			lua_settable (L, -3);
		}

		return TRUE;
	}

	return FALSE;
}

static gint
lua_task_get_symbol (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	const gchar *symbol;
	struct rspamd_metric *metric;
	GList *cur = NULL, *metric_list;
	gboolean found = FALSE;
	gint i = 1;

	symbol = luaL_checkstring (L, 2);

	if (task && symbol) {
		metric_list = g_hash_table_lookup (task->cfg->metrics_symbols, symbol);
		if (metric_list) {
			lua_createtable (L, 1, 0);
			cur = metric_list;
		}
		else {
			metric = task->cfg->default_metric;
		}

		if (!cur && metric) {
			lua_createtable (L, 1, 0);

			if ((found = lua_push_symbol_result (L, task, metric, symbol,
					NULL, TRUE, FALSE))) {
				lua_rawseti (L, -2, i++);
			}
			else {
				/* Pop table */
				lua_pop (L, 1);
			}
		}
		else {
			while (cur) {
				metric = cur->data;
				if (lua_push_symbol_result (L, task, metric, symbol,
						NULL, TRUE, FALSE)) {
					lua_rawseti (L, -2, i++);
					found = TRUE;
				}
				cur = g_list_next (cur);
			}
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	if (!found) {
		lua_pushnil (L);
	}

	return 1;
}

static gint
lua_task_has_symbol (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	const gchar *symbol;
	struct rspamd_metric_result *mres;
	gboolean found = FALSE;

	symbol = luaL_checkstring (L, 2);

	if (task && symbol) {
		mres = g_hash_table_lookup (task->results, DEFAULT_METRIC);

		if (mres) {
			found = g_hash_table_lookup (mres->symbols, symbol) != NULL;
		}

		lua_pushboolean (L, found);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_get_symbols (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	struct rspamd_metric_result *mres;
	gint i = 1;
	GHashTableIter it;
	gpointer k, v;
	struct rspamd_symbol_result *s;

	if (task) {
		mres = g_hash_table_lookup (task->results, DEFAULT_METRIC);

		if (mres) {
			lua_createtable (L, g_hash_table_size (mres->symbols), 0);
			lua_createtable (L, g_hash_table_size (mres->symbols), 0);
			g_hash_table_iter_init (&it, mres->symbols);

			while (g_hash_table_iter_next (&it, &k, &v)) {
				s = v;
				lua_pushstring (L, k);
				lua_rawseti (L, -3, i);
				lua_pushnumber (L, s->score);
				lua_rawseti (L, -2, i);
				i ++;
			}
		}
		else {
			lua_createtable (L, 0, 0);
			lua_createtable (L, 0, 0);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 2;
}

static gint
lua_task_get_symbols_all (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	struct rspamd_metric *metric;
	struct rspamd_metric_result *mres;
	GHashTableIter it;
	gpointer k, v;
	gboolean found = FALSE;
	gint i = 1;

	if (task) {
		metric = task->cfg->default_metric;
		mres = g_hash_table_lookup (task->results, DEFAULT_METRIC);
		if (mres) {
			found = TRUE;
			lua_createtable (L, g_hash_table_size (mres->symbols), 0);
			g_hash_table_iter_init (&it, mres->symbols);

			while (g_hash_table_iter_next (&it, &k, &v)) {
				lua_push_symbol_result (L, task, metric, k, v, FALSE, TRUE);
				lua_rawseti (L, -2, i++);
			}
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	if (!found) {
		lua_pushnil (L);
	}

	return 1;
}


static gint
lua_task_get_symbols_numeric (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	struct rspamd_metric_result *mres;
	gint i = 1, id;
	GHashTableIter it;
	gpointer k, v;
	struct rspamd_symbol_result *s;

	if (task) {
		mres = g_hash_table_lookup (task->results, DEFAULT_METRIC);

		if (mres) {
			lua_createtable (L, g_hash_table_size (mres->symbols), 0);
			lua_createtable (L, g_hash_table_size (mres->symbols), 0);

			g_hash_table_iter_init (&it, mres->symbols);

			while (g_hash_table_iter_next (&it, &k, &v)) {
				id = rspamd_symbols_cache_find_symbol (task->cfg->cache,
						k);
				s = v;
				lua_pushnumber (L, id);
				lua_rawseti (L, -3, i);
				lua_pushnumber (L, s->score);
				lua_rawseti (L, -2, i);
				i ++;
			}
		}
		else {
			lua_createtable (L, 0, 0);
			lua_createtable (L, 0, 0);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 2;
}

enum lua_date_type {
	DATE_CONNECT = 0,
	DATE_MESSAGE,
	DATE_CONNECT_STRING,
	DATE_MESSAGE_STRING
};

static enum lua_date_type
lua_task_detect_date_type (lua_State *L, gint idx, gboolean *gmt)
{
	enum lua_date_type type = DATE_CONNECT;

	if (lua_type (L, idx) == LUA_TNUMBER) {
		gint num = lua_tonumber (L, idx);
		if (num >= DATE_CONNECT && num <= DATE_MESSAGE_STRING) {
			return num;
		}
	}
	else if (lua_type (L, idx) == LUA_TTABLE) {
		const gchar *str;

		lua_pushvalue (L, idx);
		lua_pushstring (L, "format");
		lua_gettable (L, -2);
		str = lua_tostring (L, -1);
		if (g_ascii_strcasecmp (str, "message") == 0) {
			type = DATE_MESSAGE;
		}
		else if (g_ascii_strcasecmp (str, "connect_str") == 0) {
			type = DATE_CONNECT_STRING;
		}
		else if (g_ascii_strcasecmp (str, "message_str") == 0) {
			type = DATE_MESSAGE_STRING;
		}
		lua_pop (L, 1);

		lua_pushstring (L, "gmt");
		lua_gettable (L, -2);

		if (lua_type (L, -1) == LUA_TBOOLEAN) {
			*gmt = lua_toboolean (L, -1);
		}

		/* Value and table */
		lua_pop (L, 2);
	}

	return type;
}

static gint
lua_task_get_date (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	GPtrArray *hdrs;
	gdouble tim;
	enum lua_date_type type = DATE_CONNECT;
	gboolean gmt = TRUE;

	if (task != NULL) {
		if (lua_gettop (L) > 1) {
			type = lua_task_detect_date_type (L, 2, &gmt);
		}
		/* Get GMT date and store it to time_t */
		if (type == DATE_CONNECT || type == DATE_CONNECT_STRING) {
			tim = (tv_to_msec (&task->tv)) / 1000.;

			if (!gmt) {
				struct tm t;
				time_t tt;

				tt = tim;
				localtime_r (&tt, &t);
#if !defined(__sun)
				t.tm_gmtoff = 0;
#endif
				t.tm_isdst = 0;
				tim = mktime (&t);
			}
		}
		else {
			hdrs = rspamd_message_get_header_array (task, "Date",
					FALSE);

			if (hdrs && hdrs->len > 0) {
				time_t tt;
				struct tm t;
				struct rspamd_mime_header *h;

				h = g_ptr_array_index (hdrs, 0);
				tt = rspamd_parse_smtp_date (h->decoded, strlen (h->decoded));

				if (!gmt) {
					localtime_r (&tt, &t);
#if !defined(__sun)
					t.tm_gmtoff = 0;
#endif
					t.tm_isdst = 0;
					tim = mktime (&t);
				}
				else {
					tim = tt;
				}
			}
			else {
				tim = 0.0;
			}
		}

		if (type == DATE_CONNECT || type == DATE_MESSAGE) {
			lua_pushnumber (L, tim);
		}
		else {
			GTimeVal tv;
			gchar *out;

			double_to_tv (tim, &tv);
			out = g_time_val_to_iso8601 (&tv);
			lua_pushstring (L, out);
			g_free (out);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_get_message_id (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);

	if (task != NULL) {
		if (task->message_id != NULL) {
			lua_pushstring (L, task->message_id);
		}
		else {
			lua_pushnil (L);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_get_timeval (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);

	if (task != NULL) {
		lua_createtable (L, 0, 2);
		lua_pushstring (L, "tv_sec");
		lua_pushnumber (L, (lua_Number)task->tv.tv_sec);
		lua_settable (L, -3);
		lua_pushstring (L, "tv_usec");
		lua_pushnumber (L, (lua_Number)task->tv.tv_usec);
		lua_settable (L, -3);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_get_size (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);

	if (task != NULL) {
		lua_pushnumber (L, task->msg.len);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

/**
* - `no_log`: do not log task summary
* - `no_stat`: do not include task into scanned stats
* - `pass_all`: check all filters for task
* - `extended_urls`: output extended info about urls
* - `skip`: skip task processing
*/

#define LUA_TASK_FLAG_WRITE(flag, set) do { \
	task->flags = (set) ? (task->flags | (flag)) : (task->flags & ~(flag)); \
} while(0)

#define LUA_TASK_SET_FLAG(flag, strname, macro, set) do { \
	if (!found && strcmp ((flag), strname) == 0) { \
		LUA_TASK_FLAG_WRITE((macro), set); \
		found = TRUE; \
	} \
} while(0)

#define LUA_TASK_FLAG_READ(flag) do { \
	lua_pushboolean(L, !!(task->flags & (flag))); \
} while(0)

#define LUA_TASK_GET_FLAG(flag, strname, macro) do { \
	if (!found && strcmp ((flag), strname) == 0) { \
		LUA_TASK_FLAG_READ((macro)); \
		found = TRUE; \
	} \
} while(0)

static gint
lua_task_set_flag (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	const gchar *flag = luaL_checkstring (L, 2);
	gboolean set = TRUE, found = FALSE;

	if (lua_gettop (L) >= 3) {
		set = lua_toboolean (L, 3);
	}

	if (task != NULL && flag != NULL) {
		LUA_TASK_SET_FLAG (flag, "pass_all", RSPAMD_TASK_FLAG_PASS_ALL, set);
		LUA_TASK_SET_FLAG (flag, "no_log", RSPAMD_TASK_FLAG_NO_LOG, set);
		LUA_TASK_SET_FLAG (flag, "no_stat", RSPAMD_TASK_FLAG_NO_STAT, set);
		LUA_TASK_SET_FLAG (flag, "skip", RSPAMD_TASK_FLAG_SKIP, set);
		LUA_TASK_SET_FLAG (flag, "extended_urls", RSPAMD_TASK_FLAG_EXT_URLS, set);
		LUA_TASK_SET_FLAG (flag, "learn_spam", RSPAMD_TASK_FLAG_LEARN_SPAM, set);
		LUA_TASK_SET_FLAG (flag, "learn_ham", RSPAMD_TASK_FLAG_LEARN_HAM, set);
		LUA_TASK_SET_FLAG (flag, "broken_headers",
				RSPAMD_TASK_FLAG_BROKEN_HEADERS, set);
		LUA_TASK_SET_FLAG (flag, "greylisted", RSPAMD_TASK_FLAG_GREYLISTED, set);

		if (!found) {
			msg_warn_task ("unknown flag requested: %s", flag);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 0;
}

static gint
lua_task_has_flag (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	const gchar *flag = luaL_checkstring (L, 2);
	gboolean found = FALSE;

	if (task != NULL && flag != NULL) {
		LUA_TASK_GET_FLAG (flag, "pass_all", RSPAMD_TASK_FLAG_PASS_ALL);
		LUA_TASK_GET_FLAG (flag, "no_log", RSPAMD_TASK_FLAG_NO_LOG);
		LUA_TASK_GET_FLAG (flag, "no_stat", RSPAMD_TASK_FLAG_NO_STAT);
		LUA_TASK_GET_FLAG (flag, "skip", RSPAMD_TASK_FLAG_SKIP);
		LUA_TASK_GET_FLAG (flag, "extended_urls", RSPAMD_TASK_FLAG_EXT_URLS);
		LUA_TASK_GET_FLAG (flag, "learn_spam", RSPAMD_TASK_FLAG_LEARN_SPAM);
		LUA_TASK_GET_FLAG (flag, "learn_ham", RSPAMD_TASK_FLAG_LEARN_HAM);
		LUA_TASK_GET_FLAG (flag, "greylisted", RSPAMD_TASK_FLAG_GREYLISTED);
		LUA_TASK_GET_FLAG (flag, "broken_headers",
				RSPAMD_TASK_FLAG_BROKEN_HEADERS);

		if (!found) {
			msg_warn_task ("unknown flag requested: %s", flag);
			lua_pushboolean (L, 0);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_get_flags (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	gint idx = 1;
	guint flags, bit, i;

	if (task) {
		lua_createtable (L, 8, 0);

		flags = task->flags;

		for (i = 0; i < sizeof (task->flags) * NBBY; i ++) {
			bit = (1U << i);

			if (flags & bit) {
				switch (bit) {
				case RSPAMD_TASK_FLAG_PASS_ALL:
					lua_pushstring (L, "pass_all");
					lua_rawseti (L, -2, idx ++);
					break;
				case RSPAMD_TASK_FLAG_NO_LOG:
					lua_pushstring (L, "no_log");
					lua_rawseti (L, -2, idx++);
					break;
				case RSPAMD_TASK_FLAG_NO_STAT:
					lua_pushstring (L, "no_stat");
					lua_rawseti (L, -2, idx++);
					break;
				case RSPAMD_TASK_FLAG_SKIP:
					lua_pushstring (L, "skip");
					lua_rawseti (L, -2, idx++);
					break;
				case RSPAMD_TASK_FLAG_EXT_URLS:
					lua_pushstring (L, "extended_urls");
					lua_rawseti (L, -2, idx++);
					break;
				case RSPAMD_TASK_FLAG_BROKEN_HEADERS:
					lua_pushstring (L, "broken_headers");
					lua_rawseti (L, -2, idx++);
					break;
				case RSPAMD_TASK_FLAG_LEARN_SPAM:
					lua_pushstring (L, "learn_spam");
					lua_rawseti (L, -2, idx++);
					break;
				case RSPAMD_TASK_FLAG_LEARN_HAM:
					lua_pushstring (L, "learn_ham");
					lua_rawseti (L, -2, idx++);
					break;
				case RSPAMD_TASK_FLAG_GREYLISTED:
					lua_pushstring (L, "greylisted");
					lua_rawseti (L, -2, idx++);
					break;
				default:
					break;
				}
			}
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_get_digest (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	gchar hexbuf[33];
	gint r;

	if (task) {
		r = rspamd_encode_hex_buf (task->digest, sizeof (task->digest),
				hexbuf, sizeof (hexbuf) - 1);

		if (r > 0) {
			hexbuf[r] = '\0';
			lua_pushstring (L, hexbuf);
		}
		else {
			lua_pushnil (L);
		}

	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_learn (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	gboolean is_spam = FALSE;
	const gchar *clname = NULL;
	GError *err = NULL;
	int ret = 1;

	if (task == NULL) {
		return luaL_error (L, "invalid arguments");
	}

	is_spam = lua_toboolean(L, 2);
	if (lua_gettop (L) > 2) {
		clname = luaL_checkstring (L, 3);
	}

	if (!rspamd_learn_task_spam (task, is_spam, clname, &err)) {
		lua_pushboolean (L, FALSE);
		if (err != NULL) {
			lua_pushstring (L, err->message);
			ret = 2;
		}
	}
	else {
		lua_pushboolean (L, TRUE);
	}

	return ret;
}

static gint
lua_task_set_settings (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	ucl_object_t *settings;
	const ucl_object_t *act, *elt, *metric_elt;
	struct rspamd_metric_result *mres;
	guint i;

	settings = ucl_object_lua_import (L, 2);

	if (settings != NULL && task != NULL) {

		metric_elt = ucl_object_lookup (settings, DEFAULT_METRIC);

		if (metric_elt) {
			task->settings = ucl_object_ref (metric_elt);
			ucl_object_unref (settings);
		}
		else {
			task->settings = settings;
		}

		act = ucl_object_lookup (task->settings, "actions");

		if (act) {
			/* Adjust desired actions */
			mres = g_hash_table_lookup (task->results, DEFAULT_METRIC);

			if (mres == NULL) {
				mres = rspamd_create_metric_result (task, DEFAULT_METRIC);
			}

			for (i = 0; i < METRIC_ACTION_MAX; i++) {
				elt = ucl_object_lookup_any (act, rspamd_action_to_str (i),
						rspamd_action_to_str_alt (i), NULL);

				if (elt) {
					mres->actions_limits[i] = ucl_object_todouble (elt);
					msg_debug_task ("adjusted action %s to %.2f",
							ucl_object_key (elt), mres->actions_limits[i]);
				}
			}
		}

		rspamd_symbols_cache_process_settings (task, task->cfg->cache);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 0;
}

static gint
lua_task_set_rmilter_reply (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	ucl_object_t *reply;

	reply = ucl_object_lua_import (L, 2);

	if (reply != NULL && task != NULL) {
		rspamd_mempool_set_variable (task->task_pool, "rmilter-reply",
				reply, (rspamd_mempool_destruct_t)ucl_object_unref);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 0;
}

static gint
lua_task_get_settings (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);

	if (task != NULL) {

		if (task->settings) {
			return ucl_object_push_lua (L, task->settings, true);
		}
		else {
			lua_pushnil (L);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_lookup_settings (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	const gchar *key = NULL;
	const ucl_object_t *elt;

	if (task != NULL) {

		if (lua_isstring (L, 2)) {
			key = lua_tostring (L, 2);
		}

		if (task->settings) {
			if (key == NULL) {
				return ucl_object_push_lua (L, task->settings, true);
			}
			else {
				elt = ucl_object_lookup (task->settings, key);

				if (elt) {
					return ucl_object_push_lua (L, elt, true);
				}
				else {
					lua_pushnil (L);
				}
			}
		}
		else {
			lua_pushnil (L);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_get_settings_id (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	guint32 *hp;

	if (task != NULL) {
		hp = rspamd_mempool_get_variable (task->task_pool, "settings_hash");

		if (hp) {
			lua_pushnumber (L, *hp);
		}
		else {
			lua_pushnil (L);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_cache_get (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	const gchar *key = luaL_checkstring (L, 2);

	if (task && key) {
		if (!lua_task_get_cached (L, task, key)) {
			lua_pushnil (L);
		}
	}
	else {
		luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_cache_set (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	const gchar *key = luaL_checkstring (L, 2);

	if (task && key && lua_gettop (L) >= 3) {
		lua_task_set_cached (L, task, key, 3);
	}
	else {
		luaL_error (L, "invalid arguments");
	}

	return 0;
}

struct lua_file_cbdata {
	gchar *fname;
	gint fd;
};

static void
lua_tmp_file_dtor (gpointer p)
{
	struct lua_file_cbdata *cbdata = p;

	unlink (cbdata->fname);
	close (cbdata->fd);
}

static gint
lua_task_store_in_file (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	gchar fpath[PATH_MAX];
	guint mode = 00600;
	gint fd;
	struct lua_file_cbdata *cbdata;

	if (task) {
		if ((task->flags & RSPAMD_TASK_FLAG_FILE) && task->msg.fpath) {
			lua_pushstring (L, task->msg.fpath);
		}
		else {
			if (lua_isnumber (L, 2)) {
				mode = lua_tonumber (L, 2);
			}

			rspamd_snprintf (fpath, sizeof (fpath), "%s%c%s", task->cfg->temp_dir,
					G_DIR_SEPARATOR, "rmsg-XXXXXXXXXX");
			fd = mkstemp (fpath);

			if (fd == -1) {
				msg_err_task ("cannot save file: %s", strerror (errno));
				lua_pushnil (L);
			}
			else {
				fchmod (fd, mode);

				if (write (fd, task->msg.begin, task->msg.len) == -1) {
					msg_err_task ("cannot write file %s: %s", fpath,
							strerror (errno));
					unlink (fpath);
					close (fd);
					lua_pushnil (L);

					return 1;
				}

				cbdata = rspamd_mempool_alloc (task->task_pool, sizeof (*cbdata));
				cbdata->fd = fd;
				cbdata->fname = rspamd_mempool_strdup (task->task_pool, fpath);
				lua_pushstring (L, cbdata->fname);
				rspamd_mempool_add_destructor (task->task_pool,
						lua_tmp_file_dtor, cbdata);
			}
		}
	}
	else {
		luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_process_regexp (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	struct rspamd_lua_regexp *re = NULL;
	gboolean strong = FALSE;
	const gchar *type_str = NULL, *header_str = NULL;
	gsize header_len = 0;
	GError *err = NULL;
	gint ret = 0;
	enum rspamd_re_type type = RSPAMD_RE_BODY;

	/*
	 * - `re`* : regular expression object
 	 * - `type`*: type of regular expression:
	 *   + `mime`: mime regexp
	 *   + `rawmime`: raw mime regexp
	 *   + `header`: header regexp
	 *   + `rawheader`: raw header expression
	 *   + `body`: raw body regexp
	 *   + `url`: url regexp
	 * - `header`: for header and rawheader regexp means the name of header
	 * - `strong`: case sensitive match for headers
	 */
	if (task != NULL) {
		if (!rspamd_lua_parse_table_arguments (L, 2, &err,
					"*re=U{regexp};*type=S;header=V;strong=B",
					&re, &type_str, &header_len, &header_str,
					&strong)) {
			msg_err_task ("cannot get parameters list: %e", err);

			if (err) {
				g_error_free (err);
			}
		}
		else {
			type = rspamd_re_cache_type_from_string (type_str);

			if ((type == RSPAMD_RE_HEADER || type == RSPAMD_RE_RAWHEADER)
					&& header_str == NULL) {
				msg_err_task (
						"header argument is mandatory for header/rawheader regexps");
			}
			else {
				ret = rspamd_re_cache_process (task, re->re, type,
						(gpointer) header_str, header_len, strong);
			}
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	lua_pushnumber (L, ret);

	return 1;
}

static gint
lua_task_get_metric_score (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	const gchar *metric_name;
	gdouble rs;
	struct rspamd_metric_result *metric_res;

	metric_name = luaL_checkstring (L, 2);

	if (task && metric_name) {
		if ((metric_res =
			g_hash_table_lookup (task->results, metric_name)) != NULL) {
			lua_createtable (L, 2, 0);
			lua_pushnumber (L, isnan (metric_res->score) ? 0.0 : metric_res->score);
			rs = rspamd_task_get_required_score (task, metric_res);
			lua_rawseti (L, -2, 1);
			lua_pushnumber (L, rs);
			lua_rawseti (L, -2, 2);
		}
		else {
			lua_pushnil (L);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_get_metric_action (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	const gchar *metric_name;
	struct rspamd_metric_result *metric_res;
	enum rspamd_metric_action action;

	metric_name = luaL_checkstring (L, 2);

	if (metric_name == NULL) {
		metric_name = DEFAULT_METRIC;
	}

	if (task && metric_name) {
		if ((metric_res =
			g_hash_table_lookup (task->results, metric_name)) != NULL) {
			action = rspamd_check_action_metric (task, metric_res);
			lua_pushstring (L, rspamd_action_to_str (action));
		}
		else {
			lua_pushnil (L);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_set_metric_score (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	const gchar *metric_name;
	struct rspamd_metric_result *metric_res;
	gdouble nscore;

	metric_name = luaL_checkstring (L, 2);
	nscore = luaL_checknumber (L, 3);

	if (metric_name == NULL) {
		metric_name = DEFAULT_METRIC;
	}

	if (task && metric_name) {
		if ((metric_res =
			g_hash_table_lookup (task->results, metric_name)) != NULL) {
			msg_debug_task ("set metric score from %.2f to %.2f",
				metric_res->score, nscore);
			metric_res->score = nscore;
			lua_pushboolean (L, true);
		}
		else {
			lua_pushboolean (L, false);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_set_metric_action (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	const gchar *metric_name, *action_name;
	struct rspamd_metric_result *metric_res;
	gint action;

	metric_name = luaL_checkstring (L, 2);

	if (metric_name == NULL) {
		metric_name = DEFAULT_METRIC;
	}

	action_name = luaL_checkstring (L, 3);

	if (task && metric_name && action_name) {
		if ((metric_res =
			g_hash_table_lookup (task->results, metric_name)) != NULL) {

			if (rspamd_action_from_str (action_name, &action)) {
				metric_res->action = action;
				lua_pushboolean (L, true);
			}
			else {
				lua_pushboolean (L, false);
			}
		}
		else {
			lua_pushboolean (L, false);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_set_metric_subject (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	const gchar *subject;
	struct rspamd_metric *metric;

	metric = task->cfg->default_metric;
	subject = luaL_checkstring (L, 2);

	if (task && metric && subject) {
		metric->subject = rspamd_mempool_strdup (task->task_pool, subject);
		lua_pushboolean (L, true);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_task_get_protocol_reply (lua_State *L)
{
	struct rspamd_task *task = lua_check_task (L, 1);
	guint flags = 0;
	ucl_object_t *obj;

	if (!task) {
		return luaL_error (L, "invalid arguments");
	}

	if (!(task->processed_stages & (RSPAMD_TASK_STAGE_POST_FILTERS >> 1))) {
		return luaL_error (L, "must not be called before post-filters");
	}

	if (lua_istable (L, 2)) {
		for (lua_pushnil (L); lua_next (L, 2); lua_pop (L, 1)) {
			if (lua_isstring (L, -1)) {
				const gchar *str = lua_tostring (L, -1);

				if (strcmp (str, "default") == 0) {
					flags |= RSPAMD_PROTOCOL_DEFAULT;
				}
				else if (strcmp (str, "basic") == 0) {
					flags |= RSPAMD_PROTOCOL_BASIC;
				}
				else if (strcmp (str, "metrics") == 0) {
					flags |= RSPAMD_PROTOCOL_METRICS;
				}
				else if (strcmp (str, "messages") == 0) {
					flags |= RSPAMD_PROTOCOL_MESSAGES;
				}
				else if (strcmp (str, "rmilter") == 0) {
					flags |= RSPAMD_PROTOCOL_RMILTER;
				}
				else if (strcmp (str, "dkim") == 0) {
					flags |= RSPAMD_PROTOCOL_DKIM;
				}
				else if (strcmp (str, "extra") == 0) {
					flags |= RSPAMD_PROTOCOL_EXTRA;
				}
				else {
					msg_err_task ("invalid protocol flag: %s", str);
				}
			}
		}
	}
	else {
		flags = RSPAMD_PROTOCOL_DEFAULT;
	}

	obj = rspamd_protocol_write_ucl (task, flags);

	if (obj) {
		ucl_object_push_lua (L, obj, true);
	}
	else {
		lua_pushnil (L);
	}

	return 1;
}

/* Image functions */
static gint
lua_image_get_width (lua_State *L)
{
	struct rspamd_image *img = lua_check_image (L);

	if (img != NULL) {
		lua_pushnumber (L, img->width);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_image_get_height (lua_State *L)
{
	struct rspamd_image *img = lua_check_image (L);

	if (img != NULL) {
		lua_pushnumber (L, img->height);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_image_get_type (lua_State *L)
{
	struct rspamd_image *img = lua_check_image (L);

	if (img != NULL) {
		lua_pushstring (L, rspamd_image_type_str (img->type));
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_image_get_size (lua_State *L)
{
	struct rspamd_image *img = lua_check_image (L);

	if (img != NULL) {
		lua_pushnumber (L, img->data->len);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_image_get_filename (lua_State *L)
{
	struct rspamd_image *img = lua_check_image (L);

	if (img != NULL && img->filename != NULL) {
		lua_pushlstring (L, img->filename->begin, img->filename->len);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

/* Arvhive methods */
static gint
lua_archive_get_type (lua_State *L)
{
	struct rspamd_archive *arch = lua_check_archive (L);

	if (arch != NULL) {
		lua_pushstring (L, rspamd_archive_type_str (arch->type));
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_archive_get_files (lua_State *L)
{
	struct rspamd_archive *arch = lua_check_archive (L);
	guint i;
	struct rspamd_archive_file *f;

	if (arch != NULL) {
		lua_createtable (L, arch->files->len, 0);

		for (i = 0; i < arch->files->len; i ++) {
			f = g_ptr_array_index (arch->files, i);

			lua_pushlstring (L, f->fname->str, f->fname->len);
			lua_rawseti (L, -2, i + 1);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_archive_get_files_full (lua_State *L)
{
	struct rspamd_archive *arch = lua_check_archive (L);
	guint i;
	struct rspamd_archive_file *f;

	if (arch != NULL) {
		lua_createtable (L, arch->files->len, 0);

		for (i = 0; i < arch->files->len; i ++) {
			f = g_ptr_array_index (arch->files, i);

			lua_createtable (L, 0, 4);

			lua_pushstring (L, "name");
			lua_pushlstring (L, f->fname->str, f->fname->len);
			lua_settable (L, -3);

			lua_pushstring (L, "compressed_size");
			lua_pushnumber (L, f->compressed_size);
			lua_settable (L, -3);

			lua_pushstring (L, "uncompressed_size");
			lua_pushnumber (L, f->uncompressed_size);
			lua_settable (L, -3);

			lua_pushstring (L, "encrypted");
			lua_pushboolean (L, (f->flags & RSPAMD_ARCHIVE_FILE_ENCRYPTED) ? true : false);
			lua_settable (L, -3);

			lua_rawseti (L, -2, i + 1);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_archive_is_encrypted (lua_State *L)
{
	struct rspamd_archive *arch = lua_check_archive (L);

	if (arch != NULL) {
		lua_pushboolean (L, (arch->flags & RSPAMD_ARCHIVE_ENCRYPTED) ? true : false);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_archive_get_size (lua_State *L)
{
	struct rspamd_archive *arch = lua_check_archive (L);

	if (arch != NULL) {
		lua_pushnumber (L, arch->size);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_archive_get_filename (lua_State *L)
{
	struct rspamd_archive *arch = lua_check_archive (L);

	if (arch != NULL) {
		lua_pushlstring (L, arch->archive_name->begin, arch->archive_name->len);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

/* Text methods */
static gint
lua_text_len (lua_State *L)
{
	struct rspamd_lua_text *t = lua_check_text (L, 1);
	gsize l = 0;

	if (t != NULL) {
		l = t->len;
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	lua_pushnumber (L, l);

	return 1;
}

static gint
lua_text_str (lua_State *L)
{
	struct rspamd_lua_text *t = lua_check_text (L, 1);

	if (t != NULL) {
		lua_pushlstring (L, t->start, t->len);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_text_ptr (lua_State *L)
{
	struct rspamd_lua_text *t = lua_check_text (L, 1);

	if (t != NULL) {
		lua_pushlightuserdata (L, (gpointer)t->start);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_text_take_ownership (lua_State *L)
{
	struct rspamd_lua_text *t = lua_check_text (L, 1);
	gchar *dest;

	if (t != NULL) {
		if (t->flags & RSPAMD_TEXT_FLAG_OWN) {
			/* We already own it */
			lua_pushboolean (L, true);
		}
		else {
			dest = g_malloc (t->len);
			memcpy (dest, t->start, t->len);
			t->start = dest;
			t->flags |= RSPAMD_TEXT_FLAG_OWN;
			lua_pushboolean (L, true);
		}
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_text_save_in_file (lua_State *L)
{
	struct rspamd_lua_text *t = lua_check_text (L, 1);
	const gchar *fname = luaL_checkstring (L, 2);
	guint mode = 00644;
	gint fd;

	if (t != NULL && fname != NULL) {
		if (lua_type (L, 3) == LUA_TNUMBER) {
			mode = lua_tonumber (L, 3);
		}

		fd = rspamd_file_xopen (fname, O_CREAT | O_WRONLY | O_EXCL, mode);

		if (fd == -1) {
			lua_pushboolean (L, false);
			lua_pushstring (L, strerror (errno));

			return 2;
		}

		if (write (fd, t->start, t->len) == -1) {
			close (fd);
			lua_pushboolean (L, false);
			lua_pushstring (L, strerror (errno));

			return 2;
		}

		close (fd);
		lua_pushboolean (L, true);
	}
	else {
		return luaL_error (L, "invalid arguments");
	}

	return 1;
}

static gint
lua_text_gc (lua_State *L)
{
	struct rspamd_lua_text *t = lua_check_text (L, 1);

	if (t != NULL) {
		if (t->flags & RSPAMD_TEXT_FLAG_OWN) {
			if (t->flags & RSPAMD_TEXT_FLAG_MMAPED) {
				munmap ((gpointer)t->start, t->len);
			}
			else {
				g_free ((gpointer)t->start);
			}
		}

	}

	return 0;
}

/* Init part */

static gint
lua_load_task (lua_State * L)
{
	lua_newtable (L);
	luaL_register (L, NULL, tasklib_f);

	return 1;
}

static void
luaopen_archive (lua_State * L)
{
	rspamd_lua_new_class (L, "rspamd{archive}", archivelib_m);
	lua_pop (L, 1);
}

void
luaopen_task (lua_State * L)
{
	rspamd_lua_new_class (L, "rspamd{task}", tasklib_m);
	lua_pop (L, 1);

	rspamd_lua_add_preload (L, "rspamd_task", lua_load_task);

	luaopen_archive (L);
}

void
luaopen_image (lua_State * L)
{
	rspamd_lua_new_class (L, "rspamd{image}", imagelib_m);
	lua_pop (L, 1);
}

void
luaopen_text (lua_State *L)
{
	rspamd_lua_new_class (L, "rspamd{text}", textlib_m);
	lua_pop (L, 1);
}

void
rspamd_lua_task_push (lua_State *L, struct rspamd_task *task)
{
	struct rspamd_task **ptask;

	ptask = lua_newuserdata (L, sizeof (gpointer));
	rspamd_lua_setclass (L, "rspamd{task}", -1);
	*ptask = task;
}
