# Start the packaged program and check it stays up. Takes a screenshot for the run's
# artifacts. There's no radio or serial device on the runner, so it only proves the
# program and its Qt load and open a window.
param([Parameter(Mandatory)] [string] $Dir, [Parameter(Mandatory)] [string] $Shot)

$app = Start-Process (Join-Path $Dir 'bin/590Ctrl.exe') -PassThru
Start-Sleep -Seconds 15
Add-Type -AssemblyName System.Windows.Forms, System.Drawing
$bounds = [System.Windows.Forms.Screen]::PrimaryScreen.Bounds
$bitmap = New-Object System.Drawing.Bitmap $bounds.Width, $bounds.Height
[System.Drawing.Graphics]::FromImage($bitmap).CopyFromScreen($bounds.Location, [System.Drawing.Point]::Empty, $bounds.Size)
$bitmap.Save($Shot)
if ($app.HasExited) { throw "590Ctrl exited with code $($app.ExitCode)" }
Stop-Process -Id $app.Id -Force
