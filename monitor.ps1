# Watch the synth's serial output.
# Close this window (or press Ctrl+C) before uploading -- the upload needs the
# same COM port and will fail while this is holding it open.

$pio = "C:\Users\jacob\AppData\Local\Programs\Python\Python312\Scripts\pio.exe"
$port = "COM5"
$baud = 115200

Write-Host "Listening on $port at $baud baud. Ctrl+C to stop." -ForegroundColor Cyan
Write-Host "Press pads and move the stick -- lines should appear below." -ForegroundColor Cyan
Write-Host ""

& $pio device monitor -p $port -b $baud
