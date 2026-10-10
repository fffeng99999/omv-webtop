<?php

/**
 * This file is part of OpenMediaVault.
 *
 * @license   https://www.gnu.org/licenses/gpl.html GPL Version 3
 * @author    ${GITHUB_USER} <${GITHUB_USER}@users.noreply.github.com>
 *
 * OpenMediaVault is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * OpenMediaVault is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

namespace Engined\Rpc;

/**
 * Webtop RPC service.
 *
 * Settings (conf.service.webtop) live in the OMV configuration
 * database. The Webtop Docker Compose stack itself is owned by the
 * openmediavault-compose plugin: on save the plugin publishes (or
 * updates) the stack through the Compose RPC (Compose.setFile). The
 * stack therefore follows the Compose plugin conventions
 * (<compose shared folder>/webtop/webtop.yml + webtop.env) and shows
 * up in the Compose web UI (Services -> Compose -> Files / Services)
 * where it can be inspected, edited, started and stopped. The plugin
 * never renders those files itself, so the Compose plugin stays the
 * single writer.
 *
 * The stack is controlled through 'docker compose' (via the
 * /usr/sbin/omv-compose-run helper used by the Compose plugin) with
 * the /usr/sbin/omv-webtop-ctl helper (background tasks with live
 * output). The plugin only manages the OMV-side integration
 * (registration, lifecycle, status); Webtop itself is never modified -
 * the desktop, its applications and all settings are managed from
 * within the Webtop web interface.
 */
class OMVRpcServiceWebtop extends \OMV\Rpc\ServiceAbstract
{
    const CTL = "/usr/sbin/omv-webtop-ctl";

    /**
     * Name of the stack in the openmediavault-compose plugin. It is
     * also the name of the stack directory below the compose shared
     * folder and the base name of the generated stack files.
     */
    const STACK_NAME = "webtop";

    /**
     * Settings fields allowed to be persisted by set(). Everything else
     * sent by the form (read-only status fields) is stripped.
     */
    private $settingsFields = [
        "enable", "version", "httpPort", "httpsPort", "timeZone",
        "configRef", "sourceRef", "customUser", "password", "title",
        "puid", "pgid", "gpuAccel"
    ];

    public function getName()
    {
        return "Webtop";
    }

    public function initialize()
    {
        $this->registerMethod("get");
        $this->registerMethod("set");
        $this->registerMethod("start");
        $this->registerMethod("stop");
        $this->registerMethod("restart");
        $this->registerMethod("upgrade");
        $this->registerMethod("getLog");
        $this->registerMethod("getShowConfig");
    }

    /**
     * Run omv-webtop-ctl with the given arguments and return the
     * trimmed stdout. Throws on failure when $throwOnError is set.
     */
    private function runCtl(array $args, bool $throwOnError = true): array
    {
        $cmd = new \OMV\System\Process(self::CTL, $args);
        $cmd->setRedirect2to1();
        $cmd->setQuiet(true);
        $output = [];
        $exitStatus = 0;
        $cmd->execute($output, $exitStatus);
        if (0 !== $exitStatus && $throwOnError) {
            throw new \OMV\ExecException($cmd, $output, $exitStatus);
        }
        return [$exitStatus, $output];
    }

    private function getSettingsObject($context): array
    {
        return \OMV\Rpc\Rpc::call("Config", "get", [
            "id" => "conf.service.webtop"
        ], $context);
    }

    /**
     * Settings object enriched with read-only status information.
     */
    public function get($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);

        $object = $this->getSettingsObject($context);

        // Container state. The status text is returned as the English
        // msgid; the Workbench form page runs it through the 'translate'
        // filter (request.get.transform merges the transformed keys into
        // the response, so all other fields are preserved).
        list(, $statusOut) = $this->runCtl(["status"], false);
        $state = trim(implode("\n", $statusOut));
        if (!in_array($state, ["running", "stopped", "notinstalled"])) {
            $state = "stopped";
        }
        $object["running"] = ("running" === $state);
        $object["statusText"] = [
            "running" => "Running",
            "stopped" => "Stopped",
            "notinstalled" => "Not installed"
        ][$state];

        // Resolve the stack directory through the ctl helper (it lives
        // below the Compose plugin shared folder). Empty while the
        // Compose plugin is not configured.
        list(, $pathOut) = $this->runCtl(["path"], false);
        $dir = trim(implode("\n", $pathOut));
        $object["composeFile"] = ("" !== $dir)
            ? $dir."/".self::STACK_NAME.".yml" : "";

        return $object;
    }

    public function set($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        $data = array_intersect_key($params, array_flip($this->settingsFields));

        $result = \OMV\Rpc\Rpc::call("Config", "set", [
            "id" => "conf.service.webtop",
            "data" => $data
        ], $context);

        // Publish (create or update) the Webtop stack in the Compose
        // plugin so it follows the openmediavault-compose conventions
        // and shows up in the Compose web UI. The Compose plugin renders
        // the stack files; this plugin does not write them itself.
        $this->syncComposeStack($context, $this->getSettingsObject($context));

        return $result;
    }

    /**
     * Create or update the Webtop stack in the openmediavault-compose
     * plugin. The config and data shared folders are referenced through
     * the Compose plugin's '${{ sf:"<name>" }}' placeholder, so a
     * changed shared folder path is resolved by the Compose plugin on
     * deploy without this plugin having to re-render anything.
     */
    private function syncComposeStack($context, array $config): void
    {
        $configRef = (string)($config["configRef"] ?? "");
        $sourceRef = (string)($config["sourceRef"] ?? "");
        if ("" === $configRef) {
            // Storage not configured yet - nothing usable to publish.
            return;
        }
        $configName = $this->sharedFolderName($context, $configRef);
        if ("" === $configName) {
            throw new \OMV\Exception(
                "The selected shared folders could not be resolved."
            );
        }
        $sourceName = "";
        if ("" !== $sourceRef) {
            $sourceName = $this->sharedFolderName($context, $sourceRef);
            if ("" === $sourceName) {
                throw new \OMV\Exception(
                    "The selected shared folders could not be resolved."
                );
            }
        }

        $params = [
            "name" => self::STACK_NAME,
            "description" => "Webtop (managed by the openmediavault-webtop ".
                "plugin)",
            "body" => $this->buildComposeBody($configName, $sourceName,
                !empty($config["gpuAccel"])),
            "showenv" => true,
            "env" => $this->buildComposeEnv($config),
            "showoverride" => false,
            "override" => ""
        ];
        $uuid = $this->findStackUuid($context);
        if ("" !== $uuid) {
            $params["uuid"] = $uuid;
        }

        try {
            \OMV\Rpc\Rpc::call("Compose", "setFile", $params, $context);
        } catch (\Exception $e) {
            throw new \OMV\Exception(sprintf(
                "The Webtop stack could not be registered in the Compose ".
                "plugin: %s. Install openmediavault-compose and set the ".
                "shared folder for the compose files (Services -> Compose ".
                "-> Settings) first.",
                $e->getMessage()
            ));
        }
    }

    /**
     * UUID of the Webtop stack in the Compose plugin (empty when it
     * does not exist yet).
     */
    private function findStackUuid($context): string
    {
        $files = \OMV\Rpc\Rpc::call("Config", "get", [
            "id" => "conf.service.compose.file"
        ], $context);
        if (!is_array($files)) {
            return "";
        }
        foreach ($files as $file) {
            if (($file["name"] ?? "") === self::STACK_NAME) {
                return (string)($file["uuid"] ?? "");
            }
        }
        return "";
    }

    /**
     * Name of a shared folder by reference (empty when unknown).
     */
    private function sharedFolderName($context, string $ref): string
    {
        if ("" === $ref) {
            return "";
        }
        $folders = \OMV\Rpc\Rpc::call("Config", "get", [
            "id" => "conf.system.sharedfolder"
        ], $context);
        if (!is_array($folders)) {
            return "";
        }
        foreach ($folders as $folder) {
            if (($folder["uuid"] ?? "") === $ref) {
                return (string)($folder["name"] ?? "");
            }
        }
        return "";
    }

    /**
     * Docker Compose file body for the Webtop stack (the Compose plugin
     * prepends its own auto-generated header). The config and data
     * shared folders are injected through the %%CONFIG%%/%%SOURCE%%
     * markers so the nowdoc can stay verbatim. The data folder is
     * optional: when it is not selected the /nas mount is omitted
     * entirely (the Compose placeholder cannot resolve an empty name).
     * When $gpu is set the host GPU device (/dev/dri) is passed into the
     * container and AUTO_GPU is enabled so Webtop can use hardware
     * (zero-copy) H.264 encoding.
     */
    private function buildComposeBody(string $configName,
        string $sourceName, bool $gpu): string
    {
        $body = <<<'YAML'
name: webtop

services:
  webtop:
    container_name: webtop
    image: lscr.io/linuxserver/webtop:${WEBTOP_VERSION:-latest}
    environment:
      - PUID=${WEBTOP_PUID}
      - PGID=${WEBTOP_PGID}
      - TZ=${TZ}
      - CUSTOM_USER=${WEBTOP_USER}
      - PASSWORD=${WEBTOP_PASSWORD}
      - TITLE=${WEBTOP_TITLE}
    volumes:
      - ${{ sf:"%%CONFIG%%" }}:/config
%%SOURCE_VOLUME%%
    ports:
      - '${WEBTOP_HTTP_PORT:-3000}:3000'
      - '${WEBTOP_HTTPS_PORT:-3001}:3001'
    shm_size: "1gb"
    restart: unless-stopped
YAML;
        if ($gpu) {
            $body = str_replace(
                '      - TITLE=${WEBTOP_TITLE}',
                '      - TITLE=${WEBTOP_TITLE}'.PHP_EOL.
                '      - AUTO_GPU=true',
                $body
            );
            $body = str_replace(
                '    volumes:',
                '    devices:'.PHP_EOL.
                '      - /dev/dri:/dev/dri'.PHP_EOL.
                '    volumes:',
                $body
            );
        }
        $sourceVolume = ("" !== $sourceName)
            ? '      - ${{ sf:"%%SOURCE%%" }}:/nas'
            : "";
        $body = strtr($body, ["%%SOURCE_VOLUME%%" => $sourceVolume]);
        $body = strtr($body, [
            "%%CONFIG%%" => $configName,
            "%%SOURCE%%" => $sourceName
        ]);

        return $body."\n";
    }

    /**
     * Environment file content for the Webtop stack.
     */
    private function buildComposeEnv(array $config): string
    {
        $version = trim((string)($config["version"] ?? ""));
        if ("" === $version) {
            $version = "latest";
        }
        $lines = [
            'WEBTOP_VERSION='.$version,
            'WEBTOP_HTTP_PORT='.(string)($config["httpPort"] ?? "3000"),
            'WEBTOP_HTTPS_PORT='.(string)($config["httpsPort"] ?? "3001"),
            'TZ='.(string)($config["timeZone"] ?? "Etc/UTC"),
            'WEBTOP_PUID='.(string)($config["puid"] ?? "1000"),
            'WEBTOP_PGID='.(string)($config["pgid"] ?? "100"),
            'WEBTOP_USER='.(string)($config["customUser"] ?? "admin"),
            'WEBTOP_PASSWORD='.(string)($config["password"] ?? ""),
            'WEBTOP_TITLE='.(string)($config["title"] ?? "Webtop")
        ];
        return implode("\n", $lines)."\n";
    }

    /**
     * Run a stack operation as a background task (the WebUI shows a
     * task dialog with the live 'docker compose' output). This keeps
     * the HTTP request short even when images have to be pulled first.
     */
    private function execCtlTask($context, string $op): string
    {
        return $this->execBgCtl([$op]);
    }

    public function start($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        return $this->execCtlTask($context, "up");
    }

    public function stop($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        return $this->execCtlTask($context, "down");
    }

    public function restart($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        return $this->execCtlTask($context, "restart");
    }

    public function upgrade($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        return $this->execCtlTask($context, "upgrade");
    }

    public function getLog($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        return $this->execCtlTask($context, "logs");
    }

    public function getShowConfig($params, $context)
    {
        $this->validateMethodContext($context, ["role" => OMV_ROLE_ADMINISTRATOR]);
        return $this->execCtlTask($context, "showcompose");
    }

    /**
     * Run the ctl helper as a background task and return the task status
     * file name so the task dialog can poll the live output.
     */
    private function execBgCtl(array $args): string
    {
        return $this->execBgProc(
            function ($bgStatusFilename, $bgOutputFilename) use ($args) {
                $cmd = new \OMV\System\Process(self::CTL, $args);
                $cmd->setRedirect2to1();
                if (0 !== ($exitStatus = $this->exec(
                    $cmd, $output, $bgOutputFilename
                ))) {
                    throw new \OMV\ExecException($cmd, $output, $exitStatus);
                }
                return $output;
            }
        );
    }
}
