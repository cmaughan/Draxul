param(
    [Parameter(Mandatory = $true)]
    [string]$Executable
)

$ErrorActionPreference = 'Stop'

function Invoke-Draxul {
    param(
        [string[]]$Arguments,
        [string]$StandardInput = '',
        [int]$TimeoutMilliseconds = 20000
    )
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Executable
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    if ($StandardInput) {
        $start.RedirectStandardInput = $true
    }
    foreach ($argument in $Arguments) {
        $start.ArgumentList.Add($argument)
    }
    $process = [System.Diagnostics.Process]::Start($start)
    if ($StandardInput) {
        $process.StandardInput.Write($StandardInput)
        $process.StandardInput.Close()
    }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutMilliseconds)) {
        $process.Kill()
        $process.WaitForExit()
        throw "draxul $($Arguments -join ' ') timed out after $TimeoutMilliseconds ms"
    }
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    if ($process.ExitCode -ne 0) {
        throw "draxul $($Arguments -join ' ') failed ($($process.ExitCode)): $stderr"
    }
    return $stdout
}

$runtime = Join-Path ([System.IO.Path]::GetTempPath()) (
    'draxul-topology-cli-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runtime | Out-Null
$serverPid = $null
$endpointPath = $null

$layout = @{
    name = 'Automation'
    alias = 'space'
    root_directory = (Split-Path -Parent (Split-Path -Parent $Executable))
    tabs = @(
        @{
            name = 'Workers'
            alias = 'workers'
            panes = @(
                @{ name = 'Shell'; alias = 'shell' }
                @{
                    name = 'Tests'
                    alias = 'tests'
                    split_from = 'shell'
                    direction = 'right'
                    ratio = 0.6
                }
                @{
                    name = 'Logs'
                    alias = 'logs'
                    split_from = 'tests'
                    direction = 'down'
                    ratio = 0.4
                }
                @{
                    name = 'Triangle'
                    alias = 'triangle'
                    split_from = 'logs'
                    direction = 'right'
                    plugin_id = 'dev.draxul.spinning-triangle'
                    plugin_config = @{
                        paused = $true
                        initial_angle = 0.5
                    }
                }
            )
        }
    )
} | ConvertTo-Json -Depth 10 -Compress

try {
    Invoke-Draxul @('--server', '--server-runtime-dir', $runtime) | Out-Null
    for ($attempt = 0; $attempt -lt 120; ++$attempt) {
        $endpoint = Get-ChildItem -LiteralPath $runtime `
            -Filter '*.control.json' -File -ErrorAction SilentlyContinue |
            Select-Object -First 1
        if ($endpoint) {
            try {
                $metadata = Get-Content -LiteralPath $endpoint.FullName -Raw |
                    ConvertFrom-Json
                if ($metadata.state -eq 'ready' -and $metadata.server_pid) {
                    $endpointPath = $endpoint.FullName
                    $serverPid = [int]$metadata.server_pid
                    break
                }
            }
            catch {
                # The server publishes through an atomic rename, but tolerate a
                # transient read while antivirus/indexing observes the file.
            }
        }
        Start-Sleep -Milliseconds 100
    }
    if (-not $endpointPath) {
        throw 'The isolated Draxul server did not publish a ready endpoint.'
    }

    $before = @(Invoke-Draxul @(
        'space', 'list', '--json', '--server-runtime-dir', $runtime) |
        ConvertFrom-Json)
    $validation = Invoke-Draxul @(
        'layout', 'validate', '-', '--json',
        '--server-runtime-dir', $runtime) $layout | ConvertFrom-Json
    $afterValidation = @(Invoke-Draxul @(
        'space', 'list', '--json', '--server-runtime-dir', $runtime) |
        ConvertFrom-Json)
    if (-not $validation.valid -or $before.Count -ne $afterValidation.Count) {
        throw 'Layout validation mutated the server or did not report valid.'
    }

    $applied = Invoke-Draxul @(
        'layout', 'apply', '-', '--json',
        '--server-runtime-dir', $runtime) $layout | ConvertFrom-Json
    $panes = @(Invoke-Draxul @(
        'pane', 'list', '--space', $applied.created_id, '--json',
        '--server-runtime-dir', $runtime) | ConvertFrom-Json)
    if ($panes.Count -ne 4 -or -not $applied.aliases.tests) {
        throw 'The applied layout does not contain the expected panes and aliases.'
    }
    $triangle = $panes | Where-Object id -eq $applied.aliases.triangle
    if (-not $triangle -or $triangle.domain -ne 'client_local' `
        -or $triangle.client_host_kind -ne 'plugin' `
        -or $triangle.client_plugin_id -ne 'dev.draxul.spinning-triangle' `
        -or $triangle.terminal_id) {
        throw ('Declarative plugin pane allocated a terminal or lost its descriptor: ' `
            + ($triangle | ConvertTo-Json -Depth 5 -Compress))
    }

    $pluginList = @(Invoke-Draxul @('plugin', 'list', '--json') |
        ConvertFrom-Json)
    if (-not ($pluginList | Where-Object id -eq 'dev.draxul.spinning-triangle')) {
        throw 'Bundled plugin was not discoverable through plugin list.'
    }
    if (-not ($pluginList | Where-Object id -eq 'dev.draxul.satview')) {
        throw 'Bundled SatView plugin was not discoverable through plugin list.'
    }
    if (-not ($pluginList | Where-Object id -eq 'dev.draxul.scoreview')) {
        throw 'Bundled ScoreView plugin was not discoverable through plugin list.'
    }
    $satViewPlugin = Invoke-Draxul @(
        'plugin', 'get', 'dev.draxul.satview', '--json') | ConvertFrom-Json
    if (-not $satViewPlugin.available -or $satViewPlugin.abi_version -ne 2) {
        throw 'Bundled SatView plugin metadata is unavailable or incompatible.'
    }
    $scoreViewPlugin = Invoke-Draxul @(
        'plugin', 'get', 'dev.draxul.scoreview', '--json') | ConvertFrom-Json
    if (-not $scoreViewPlugin.available -or $scoreViewPlugin.abi_version -ne 2) {
        throw 'Bundled ScoreView plugin metadata is unavailable or incompatible.'
    }

    $pluginTab = Invoke-Draxul @(
        'tab', 'create', '--space', $applied.created_id,
        '--name', 'Plugin Tab', '--plugin', 'dev.draxul.spinning-triangle',
        '--plugin-config', '{"paused":true}', '--json',
        '--server-runtime-dir', $runtime) | ConvertFrom-Json
    $pluginTabState = Invoke-Draxul @(
        'tab', 'get', $pluginTab.created_id, '--json',
        '--server-runtime-dir', $runtime) | ConvertFrom-Json
    if ($pluginTabState.panes.Count -ne 1 `
        -or $pluginTabState.panes[0].terminal_id `
        -or $pluginTabState.panes[0].client_plugin_id -ne 'dev.draxul.spinning-triangle') {
        throw 'Plugin tab creation did not preserve a terminal-free plugin pane.'
    }

    $pluginSplit = Invoke-Draxul @(
        'pane', 'split', $applied.aliases.shell, '--direction', 'right',
        '--plugin', 'dev.draxul.spinning-triangle',
        '--plugin-config', '{"paused":true}', '--json',
        '--server-runtime-dir', $runtime) | ConvertFrom-Json
    $pluginSplitState = Invoke-Draxul @(
        'pane', 'get', $pluginSplit.created_id, '--json',
        '--server-runtime-dir', $runtime) | ConvertFrom-Json
    if ($pluginSplitState.terminal_id `
        -or $pluginSplitState.client_plugin_id -ne 'dev.draxul.spinning-triangle') {
        throw 'Plugin pane split allocated a terminal or lost its descriptor.'
    }

    $satViewTab = Invoke-Draxul @(
        'tab', 'create', '--space', $applied.created_id,
        '--name', 'SatView', '--plugin', 'dev.draxul.satview',
        '--plugin-config', '{"paused":true}', '--json',
        '--server-runtime-dir', $runtime) | ConvertFrom-Json
    $satViewTabState = Invoke-Draxul @(
        'tab', 'get', $satViewTab.created_id, '--json',
        '--server-runtime-dir', $runtime) | ConvertFrom-Json
    if ($satViewTabState.panes.Count -ne 1 `
        -or $satViewTabState.panes[0].terminal_id `
        -or $satViewTabState.panes[0].client_plugin_id -ne 'dev.draxul.satview') {
        throw 'SatView tab creation did not preserve a terminal-free plugin pane.'
    }

    $scoreViewTab = Invoke-Draxul @(
        'tab', 'create', '--space', $applied.created_id,
        '--name', 'ScoreView', '--plugin', 'dev.draxul.scoreview',
        '--plugin-config', '{"source":"tests/fixtures/musicxml/swan_lake.musicxml","mode":"paged"}',
        '--json', '--server-runtime-dir', $runtime) | ConvertFrom-Json
    $scoreViewTabState = Invoke-Draxul @(
        'tab', 'get', $scoreViewTab.created_id, '--json',
        '--server-runtime-dir', $runtime) | ConvertFrom-Json
    if ($scoreViewTabState.panes.Count -ne 1 `
        -or $scoreViewTabState.panes[0].terminal_id `
        -or $scoreViewTabState.panes[0].client_plugin_id -ne 'dev.draxul.scoreview' `
        -or $scoreViewTabState.panes[0].client_plugin_config_json -notlike '*swan_lake*') {
        throw 'ScoreView tab creation did not preserve its terminal-free plugin descriptor.'
    }

    $marker = "DRAXUL-CONTEXT:$($applied.created_id):$($applied.aliases.workers):$($applied.aliases.shell)"
    $contextCommand = 'Write-Output ("DRAXUL-CONTEXT:" + ' +
        '$env:DRAXUL_SPACE_ID + ":" + $env:DRAXUL_TAB_ID + ":" + ' +
        '$env:DRAXUL_PANE_ID)'
    Invoke-Draxul @(
        'pane', 'run', $applied.aliases.shell, '--command', $contextCommand,
        '--json', '--server-runtime-dir', $runtime) | Out-Null
    $waited = Invoke-Draxul @(
        'pane', 'wait-output', $applied.aliases.shell, '--text', $marker,
        '--timeout', '15s', '--json', '--server-runtime-dir', $runtime) |
        ConvertFrom-Json
    if ($waited.text -notlike "*$marker*") {
        throw 'The pane process did not receive its stable Draxul route context.'
    }

    $quotedExecutable = $Executable.Replace("'", "''")
    $currentCommand = "& '$quotedExecutable' pane get --current --json"
    Invoke-Draxul @(
        'pane', 'run', $applied.aliases.shell, '--command', $currentCommand,
        '--json', '--server-runtime-dir', $runtime) | Out-Null
    $currentMarker = '"id": "' + $applied.aliases.shell + '"'
    $current = Invoke-Draxul @(
        'pane', 'wait-output', $applied.aliases.shell,
        '--text', $currentMarker, '--timeout', '15s', '--json',
        '--server-runtime-dir', $runtime) | ConvertFrom-Json
    if ($current.text -notlike "*$currentMarker*") {
        throw '--current did not resolve the enclosing pane without explicit routing.'
    }

    Invoke-Draxul @(
        'pane', 'move', $applied.aliases.logs,
        '--target', $applied.aliases.shell, '--direction', 'left',
        '--json', '--server-runtime-dir', $runtime) | Out-Null
    $splits = @(Invoke-Draxul @(
        'split', 'list', '--tab', $applied.aliases.workers, '--json',
        '--server-runtime-dir', $runtime) | ConvertFrom-Json)
    if ($splits.Count -ne 4) {
        throw 'Pane movement did not preserve a valid five-pane split tree.'
    }

    Write-Output "Topology CLI integration passed: $($applied.created_id)"
}
finally {
    try {
        Invoke-Draxul @(
            '--shutdown-server', '--yes',
            '--server-runtime-dir', $runtime) | Out-Null
    }
    catch {
        Write-Warning "Could not stop isolated test server: $_"
    }
    if ($serverPid) {
        try {
            Wait-Process -Id $serverPid -Timeout 10 -ErrorAction Stop
        }
        catch {
            $survivor = Get-CimInstance Win32_Process `
                -Filter "ProcessId = $serverPid" -ErrorAction SilentlyContinue
            $isExpectedServer = $survivor `
                -and $survivor.Name -eq 'draxul-server.exe' `
                -and $survivor.CommandLine -like "*$runtime*"
            if ($isExpectedServer) {
                Stop-Process -Id $serverPid -Force -ErrorAction Stop
                Write-Warning "Force-stopped isolated test server PID $serverPid after graceful shutdown timed out."
            }
            elseif ($survivor) {
                throw "Refused to stop reused or unexpected PID $serverPid after isolated-server shutdown timed out."
            }
        }
    }
    if ($endpointPath -and (Test-Path -LiteralPath $endpointPath)) {
        throw "The isolated server endpoint survived shutdown: $endpointPath"
    }
}
