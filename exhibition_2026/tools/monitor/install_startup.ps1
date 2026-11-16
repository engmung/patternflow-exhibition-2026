<#
  One-time setup for the Patternflow exhibit monitor.

  Makes two shortcuts named "패턴플로우 모니터" that run start_monitor.bat
  (minimised): one in this user's Startup folder, so the monitor and its
  window open by themselves after the (auto-)login, and one on the Desktop,
  which the dashboard and README tell staff to double-click when the monitor
  is off.

    install_startup.bat            make the shortcuts (again: they are overwritten)
    install_startup.bat -Remove    take them away

  Nothing else is changed: no scheduled task, no registry, no power settings.
  This file must stay UTF-8 with BOM (Windows PowerShell 5.1 reads a file
  without BOM in the ANSI codepage and the Korean name would break).
#>
param([switch]$Remove)
$ErrorActionPreference = 'Stop'

$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$bat = Join-Path $here 'start_monitor.bat'
$name = '패턴플로우 모니터.lnk'
$folders = @(
    @{ Label = 'Startup'; Path = [Environment]::GetFolderPath('Startup') },
    @{ Label = 'Desktop'; Path = [Environment]::GetFolderPath('Desktop') }
)

if (-not $Remove) {
    if (-not (Test-Path -LiteralPath $bat)) { throw "start_monitor.bat not found next to this script: $bat" }
    # The monitor needs Python with python-rtmidi; say so now rather than at the venue.
    $py = Get-Command pythonw.exe -ErrorAction SilentlyContinue
    if (-not $py) { Write-Warning 'pythonw.exe is not on PATH - the monitor will run in a minimised console window instead.' }
    $probe = Get-Command python.exe -ErrorAction SilentlyContinue
    if ($probe) {
        & $probe.Source -c "import rtmidi" 2>$null
        if ($LASTEXITCODE -ne 0) { Write-Warning 'python-rtmidi is missing: run  python -m pip install python-rtmidi  (the dashboard works without it but cannot see MIDI).' }
    } else {
        Write-Warning 'python.exe is not on PATH - install Python 3.11 first.'
    }
}

$shell = New-Object -ComObject WScript.Shell
foreach ($f in $folders) {
    $lnk = Join-Path $f.Path $name
    if ($Remove) {
        if (Test-Path -LiteralPath $lnk) {
            Remove-Item -LiteralPath $lnk
            Write-Host ("removed  {0}: {1}" -f $f.Label, $lnk)
        } else {
            Write-Host ("nothing  {0}: {1}" -f $f.Label, $lnk)
        }
        continue
    }
    $s = $shell.CreateShortcut($lnk)
    $s.TargetPath = $bat
    $s.WorkingDirectory = $here
    $s.WindowStyle = 7          # minimised: the batch file's console only flashes in the taskbar
    $s.Description = 'Patternflow exhibit monitor (http://127.0.0.1:8765/)'
    $s.IconLocation = "$env:SystemRoot\System32\shell32.dll,15"
    $s.Save()
    Write-Host ("created  {0}: {1}" -f $f.Label, $lnk)
}
