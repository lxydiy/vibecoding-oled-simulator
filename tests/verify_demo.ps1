param(
    [string]$Executable = 'build/Release/oled_simulator.exe',
    [string]$OutputDirectory = 'build/screenshots'
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class OledDemoWindow {
    [StructLayout(LayoutKind.Sequential)]
    public struct Rect { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr hwnd, out Rect rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd, int command);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hwnd, uint msg, IntPtr wp, IntPtr lp);
    private delegate bool EnumCallback(IntPtr hwnd, IntPtr param);
    [DllImport("user32.dll")] private static extern bool EnumWindows(EnumCallback callback, IntPtr param);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] private static extern int GetClassName(IntPtr hwnd, StringBuilder name, int count);
    public static IntPtr FindForProcess(uint processId) {
        IntPtr found = IntPtr.Zero;
        EnumWindows(delegate(IntPtr hwnd, IntPtr param) {
            uint pid;
            GetWindowThreadProcessId(hwnd, out pid);
            if (pid != processId) return true;
            StringBuilder name = new StringBuilder(128);
            GetClassName(hwnd, name, name.Capacity);
            if (name.ToString() != "HeaterOLEDSimulatorWindow") return true;
            found = hwnd;
            return false;
        }, IntPtr.Zero);
        return found;
    }
}
'@

$executablePath = (Resolve-Path -LiteralPath $Executable).Path
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$outputPath = (Resolve-Path -LiteralPath $OutputDirectory).Path
$demoProcess = Start-Process -FilePath $executablePath -WindowStyle Hidden -PassThru
function Save-DemoFrame([IntPtr]$window, [string]$name) {
    $rect = New-Object OledDemoWindow+Rect
    if (![OledDemoWindow]::GetClientRect($window, [ref]$rect)) { throw 'Cannot read window size' }
    $bitmap = New-Object Drawing.Bitmap($rect.Right, $rect.Bottom)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $dc = $graphics.GetHdc()
    try {
        if (![OledDemoWindow]::PrintWindow($window, $dc, 1)) { throw 'Cannot capture OLED window' }
    } finally {
        $graphics.ReleaseHdc($dc)
        $graphics.Dispose()
    }
    try { $bitmap.Save((Join-Path $outputPath $name), [Drawing.Imaging.ImageFormat]::Png) }
    finally { $bitmap.Dispose() }
}
try {
    $window = [IntPtr]::Zero
    for ($attempt = 0; $attempt -lt 100; ++$attempt) {
        Start-Sleep -Milliseconds 50
        $demoProcess.Refresh()
        if ($demoProcess.HasExited) { throw 'Demo exited before opening a window' }
        $window = [OledDemoWindow]::FindForProcess($demoProcess.Id)
        if ($window -ne [IntPtr]::Zero) { break }
    }
    if ($window -eq [IntPtr]::Zero) { throw 'OLED window did not open' }
    # Keep the helper console hidden but show the actual GUI under test.
    [void][OledDemoWindow]::ShowWindow($window, 4)
    Start-Sleep -Milliseconds 100
    Save-DemoFrame $window 'demo.png'
    [void][OledDemoWindow]::SendMessage($window, 0x100, [IntPtr]0x31, [IntPtr]0)
    [void][OledDemoWindow]::SendMessage($window, 0x100, [IntPtr]0x33, [IntPtr]0)
    Start-Sleep -Milliseconds 100
    Save-DemoFrame $window 'demo-keys-1-3.png'
    [void][OledDemoWindow]::SendMessage($window, 0x101, [IntPtr]0x31, [IntPtr]0)
    [void][OledDemoWindow]::SendMessage($window, 0x101, [IntPtr]0x33, [IntPtr]0)
    Start-Sleep -Milliseconds 100
    Save-DemoFrame $window 'demo-released.png'
    $initial = (Get-FileHash -LiteralPath (Join-Path $outputPath 'demo.png')).Hash
    $pressed = (Get-FileHash -LiteralPath (Join-Path $outputPath 'demo-keys-1-3.png')).Hash
    $released = (Get-FileHash -LiteralPath (Join-Path $outputPath 'demo-released.png')).Hash
    if ($initial -eq $pressed) { throw 'Key presses did not change the demo frame' }
    if ($initial -ne $released) { throw 'Key releases did not restore the demo frame' }
    [void][OledDemoWindow]::SendMessage($window, 0x10, [IntPtr]0, [IntPtr]0)
    if (!$demoProcess.WaitForExit(5000)) { throw 'Demo did not exit after closing its window' }
    if ($demoProcess.ExitCode -ne 0) { throw "Demo exit code: $($demoProcess.ExitCode)" }
    Write-Output "PASS: demo rendering, keys 1+3, release restoration, close; images in $outputPath"
} finally {
    if (!$demoProcess.HasExited) { Stop-Process -Id $demoProcess.Id }
    $demoProcess.Dispose()
}
