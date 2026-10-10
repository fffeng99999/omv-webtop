#!/usr/bin/env dash
#
# This file is part of OpenMediaVault.
#
# @license   https://www.gnu.org/licenses/gpl.html GPL Version 3
# @author    ${GITHUB_USER} <${GITHUB_USER}@users.noreply.github.com>
#
# OpenMediaVault is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# any later version.

set -e

. /usr/share/openmediavault/scripts/helper-functions

########################################################################
# Update the configuration.
# <config>
#   <services>
#     <webtop>
#       <enable>0|1</enable>
#       <version>latest</version>
#       <httpPort>3000</httpPort>
#       <httpsPort>3001</httpsPort>
#       <timeZone>Etc/UTC</timeZone>
#       <configRef>uuid-or-empty</configRef>
#       <sourceRef>uuid-or-empty</sourceRef>
#       <customUser>admin</customUser>
#       <password></password>
#       <title>Webtop</title>
#       <puid>1000</puid>
#       <pgid>1000</pgid>
#     </webtop>
#   </services>
# </config>
########################################################################
if ! omv_config_exists "/config/services/webtop"; then
	omv_config_add_node "/config/services" "webtop"
	omv_config_add_key "/config/services/webtop" "enable" "0"
	omv_config_add_key "/config/services/webtop" "version" "latest"
	omv_config_add_key "/config/services/webtop" "httpPort" "3000"
	omv_config_add_key "/config/services/webtop" "httpsPort" "3001"
	omv_config_add_key "/config/services/webtop" "timeZone" "Etc/UTC"
	omv_config_add_key "/config/services/webtop" "configRef" ""
	omv_config_add_key "/config/services/webtop" "sourceRef" ""
	omv_config_add_key "/config/services/webtop" "customUser" "admin"
	omv_config_add_key "/config/services/webtop" "password" ""
	omv_config_add_key "/config/services/webtop" "title" "Webtop"
	omv_config_add_key "/config/services/webtop" "puid" "1000"
	omv_config_add_key "/config/services/webtop" "pgid" "100"
	omv_config_add_key "/config/services/webtop" "gpuAccel" "0"
fi

exit 0
