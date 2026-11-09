param([switch]$ValidateOnly, [ValidateSet('Debug','Release')][string]$Configuration='Release')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
New-Item -ItemType Directory -Force -Path (Join-Path $projectRoot 'reports') | Out-Null

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()

$settingsPath = Join-Path $projectRoot 'private\launcher-usa.json'
$nativePath = Join-Path $projectRoot ('prototype-usa\out\build\win-amd64-' + $Configuration.ToLower() + '\majin.exe')
$has60 = Test-Path -LiteralPath $nativePath

# =========================================================================
# Main Form (Clean Modern Dark Theme)
# =========================================================================
$form = New-Object System.Windows.Forms.Form
$form.Text = 'Majistic Recomp - Launcher'
$form.ClientSize = New-Object System.Drawing.Size(530, 674)
$form.StartPosition = 'CenterScreen'
$form.FormBorderStyle = 'FixedDialog'
$form.MaximizeBox = $false
$form.Font = New-Object System.Drawing.Font('Segoe UI', 9.25)
$form.BackColor = [System.Drawing.Color]::FromArgb(24, 26, 32)
$form.ForeColor = [System.Drawing.Color]::FromArgb(226, 232, 240)

# Optional icon loading
$iconPath = Join-Path $projectRoot 'launcher\launcher.ico'
if (Test-Path -LiteralPath $iconPath) {
    try { $form.Icon = New-Object System.Drawing.Icon($iconPath) } catch {}
}

# Style palettes
$cCardBg    = [System.Drawing.Color]::FromArgb(32, 35, 44)
$cControlBg = [System.Drawing.Color]::FromArgb(44, 48, 60)
$cTextMain  = [System.Drawing.Color]::FromArgb(241, 245, 249)
$cTextMuted = [System.Drawing.Color]::FromArgb(148, 163, 184)
$cBorder    = [System.Drawing.Color]::FromArgb(55, 61, 76)
$cAccent    = [System.Drawing.Color]::FromArgb(56, 189, 248) # Cyan

# =========================================================================
# Header Section
# =========================================================================
$headerTitle = New-Object System.Windows.Forms.Label
$headerTitle.Text = 'MAJISTIC RECOMP'
$headerTitle.Font = New-Object System.Drawing.Font('Segoe UI', 13.5, [System.Drawing.FontStyle]::Bold)
$headerTitle.ForeColor = [System.Drawing.Color]::FromArgb(248, 250, 252)
$headerTitle.SetBounds(20, 14, 490, 26)
$form.Controls.Add($headerTitle)

$headerSub = New-Object System.Windows.Forms.Label
$headerSub.Text = 'PC Recompilation Settings & Launch Configuration'
$headerSub.Font = New-Object System.Drawing.Font('Segoe UI', 9.0)
$headerSub.ForeColor = $cTextMuted
$headerSub.SetBounds(21, 40, 490, 18)
$form.Controls.Add($headerSub)

$headerLine = New-Object System.Windows.Forms.Panel
$headerLine.SetBounds(20, 64, 490, 1)
$headerLine.BackColor = $cBorder
$form.Controls.Add($headerLine)

# =========================================================================
# Helper Functions for UI Construction
# =========================================================================
function New-SectionCard($title, $y, $height) {
    $grp = New-Object System.Windows.Forms.GroupBox
    $grp.Text = "  $title  "
    $grp.SetBounds(20, $y, 490, $height)
    $grp.Font = New-Object System.Drawing.Font('Segoe UI', 8.5, [System.Drawing.FontStyle]::Bold)
    $grp.ForeColor = $cAccent
    $grp.BackColor = $cCardBg
    $grp.FlatStyle = 'Flat'
    $form.Controls.Add($grp)
    return $grp
}

function Add-FieldLabel($parent, $text, $x, $y, $w) {
    $lbl = New-Object System.Windows.Forms.Label
    $lbl.Text = $text
    $lbl.SetBounds($x, $y + 3, $w, 20)
    $lbl.Font = New-Object System.Drawing.Font('Segoe UI', 9.0)
    $lbl.ForeColor = $cTextMain
    $lbl.BackColor = [System.Drawing.Color]::Transparent
    $parent.Controls.Add($lbl)
}

function Add-Dropdown($parent, $x, $y, $w, $items, $defaultIndex=0) {
    $cb = New-Object System.Windows.Forms.ComboBox
    $cb.DropDownStyle = 'DropDownList'
    $cb.SetBounds($x, $y, $w, 26)
    $cb.Font = New-Object System.Drawing.Font('Segoe UI', 9.0)
    $cb.BackColor = $cControlBg
    $cb.ForeColor = $cTextMain
    $cb.FlatStyle = 'Flat'
    $cb.Items.AddRange([object[]]$items)
    $cb.SelectedIndex = $defaultIndex
    $parent.Controls.Add($cb)
    return $cb
}

# Tooltip manager
$tooltip = New-Object System.Windows.Forms.ToolTip
$tooltip.AutoPopDelay = 8000
$tooltip.InitialDelay = 400

# =========================================================================
# Section 1: Display & Graphics
# =========================================================================
$grpDisplay = New-SectionCard 'DISPLAY & GRAPHICS' 76 132

Add-FieldLabel $grpDisplay 'Window size:' 16 26 100
$size = Add-Dropdown $grpDisplay 120 23 150 @('1280x720','1920x1080','2560x1440','3840x2160') 1

Add-FieldLabel $grpDisplay 'Mode:' 285 26 48
$mode = Add-Dropdown $grpDisplay 338 23 136 @('Windowed','Fullscreen') 0

Add-FieldLabel $grpDisplay 'Render scale:' 16 60 100
$scale = Add-Dropdown $grpDisplay 120 57 354 @(
    '1x - Native (720p)',
    '2x - 1440p (QHD - Recommended)',
    '3x - 2160p (4K UHD)'
) 1

Add-FieldLabel $grpDisplay 'Anti-aliasing:' 16 94 100
$effect = Add-Dropdown $grpDisplay 120 91 150 @(
    'FXAA (Recommended)',
    'Original (None)'
) 0

Add-FieldLabel $grpDisplay 'Upscaler:' 278 94 60
$upscaler = Add-Dropdown $grpDisplay 338 91 136 @('FSR 1','CAS','Bilinear') 0

$tooltip.SetToolTip($size, 'Target window resolution for display.')
$tooltip.SetToolTip($mode, 'Display in a standard window or borderless fullscreen.')
$tooltip.SetToolTip($scale, 'Internal 3D rendering resolution scale.')
$tooltip.SetToolTip($effect, 'Post-processing anti-aliasing.')
$tooltip.SetToolTip($upscaler, 'How the rendered image is scaled to the window. FSR 1: AMD edge-adaptive upscaling + sharpening, works on any GPU and makes 1x look much sharper for almost no GPU cost. CAS: sharpening only. Bilinear: plain stretch (original behavior).')

# =========================================================================
# Section 2: Performance & Engine
# =========================================================================
$grpPerf = New-SectionCard 'PERFORMANCE & ENGINE' 218 98

Add-FieldLabel $grpPerf 'Framerate:' 16 26 100
$frameRate = Add-Dropdown $grpPerf 120 23 150 @('60 FPS (Recommended)','Original (30 FPS)') 0
$frameRate.Enabled = $has60

Add-FieldLabel $grpPerf 'VSync:' 285 26 48
$sync = Add-Dropdown $grpPerf 338 23 136 @('Off (Recommended)','On') 0

Add-FieldLabel $grpPerf 'Rendering path:' 16 60 100
$renderingPath = Add-Dropdown $grpPerf 120 57 354 @(
    'Vulkan (FBO Host - Recommended)',
    'Direct3D 12 (Default RTV/ROV)',
    'Direct3D 12 (Forced ROV)'
) 0

$tooltip.SetToolTip($frameRate, 'Unlocks 60 FPS gameplay with normalized cutscene timers, or locks to original 30 FPS.')
$tooltip.SetToolTip($sync, 'Enables vertical sync to prevent screen tearing.')
$tooltip.SetToolTip($renderingPath, 'D3D12 e Vulkan FBO utilizam host render targets padrão acelerados por hardware.')

# =========================================================================
# Section 3: Startup & Cutscenes
# =========================================================================
$grpCutscene = New-SectionCard 'STARTUP & CUTSCENES' 326 126

$chkSkipLogos = New-Object System.Windows.Forms.CheckBox
$chkSkipLogos.Text = 'Skip opening logos (boot directly to start screen)'
$chkSkipLogos.SetBounds(16, 22, 458, 22)
$chkSkipLogos.Font = New-Object System.Drawing.Font('Segoe UI', 9.0, [System.Drawing.FontStyle]::Bold)
$chkSkipLogos.ForeColor = $cTextMain
$chkSkipLogos.BackColor = [System.Drawing.Color]::Transparent
$chkSkipLogos.FlatStyle = 'Flat'
$chkSkipLogos.Checked = $false
$grpCutscene.Controls.Add($chkSkipLogos)

$cutsceneEnabled = New-Object System.Windows.Forms.CheckBox
$cutsceneEnabled.Text = 'Enable fast-forward (Hold Select on gamepad or Tab on keyboard)'
$cutsceneEnabled.SetBounds(16, 52, 458, 22)
$cutsceneEnabled.Font = New-Object System.Drawing.Font('Segoe UI', 9.0, [System.Drawing.FontStyle]::Bold)
$cutsceneEnabled.ForeColor = $cTextMain
$cutsceneEnabled.BackColor = [System.Drawing.Color]::Transparent
$cutsceneEnabled.FlatStyle = 'Flat'
$cutsceneEnabled.Checked = $true
$grpCutscene.Controls.Add($cutsceneEnabled)

Add-FieldLabel $grpCutscene 'Speed multiplier:' 16 86 115
$cutsceneSpeed = Add-Dropdown $grpCutscene 135 83 135 @('2x Speed','4x Speed (Default)','6x Speed','8x Speed') 1

$cutsceneHint = New-Object System.Windows.Forms.Label
$cutsceneHint.Text = 'Only accelerates during in-game dialogue & 3D scenes'
$cutsceneHint.SetBounds(278, 87, 196, 20)
$cutsceneHint.Font = New-Object System.Drawing.Font('Segoe UI', 8.0)
$cutsceneHint.ForeColor = $cTextMuted
$cutsceneHint.BackColor = [System.Drawing.Color]::Transparent
$grpCutscene.Controls.Add($cutsceneHint)

$speedMap = @(2, 4, 6, 8)

$cutsceneEnabled.Add_CheckedChanged({
    $cutsceneSpeed.Enabled = $cutsceneEnabled.Checked
    $cutsceneHint.ForeColor = if ($cutsceneEnabled.Checked) { $cTextMuted } else { [System.Drawing.Color]::FromArgb(90, 95, 105) }
})

$tooltip.SetToolTip($chkSkipLogos, 'Bypasses opening publisher and developer logos, taking you straight to PRESS START BUTTON.')
$tooltip.SetToolTip($cutsceneEnabled, 'Allows speeding through in-game cutscenes and dialogues without breaking audio or muting subsequent voices.')
$tooltip.SetToolTip($cutsceneSpeed, 'Sets the speed multiplier for game logic and audio pitch-scaling during fast-forward.')

# =========================================================================
# Section 4: Controls & Directory
# =========================================================================
$grpControls = New-SectionCard 'CONTROLS & GAME DIRECTORY' 462 134

Add-FieldLabel $grpControls 'Input mode:' 16 26 100
$inputMode = Add-Dropdown $grpControls 120 23 150 @('Controller','Keyboard + mouse') 0

$controlHelp = New-Object System.Windows.Forms.Button
$controlHelp.Text = 'Configure KBM & Sens...'
$controlHelp.SetBounds(280, 23, 194, 26)
$controlHelp.FlatStyle = 'Flat'
$controlHelp.Font = New-Object System.Drawing.Font('Segoe UI', 8.5)
$controlHelp.BackColor = $cControlBg
$controlHelp.ForeColor = $cTextMain
$controlHelp.FlatAppearance.BorderColor = $cBorder
$grpControls.Controls.Add($controlHelp)
$script:mouseSensitivity = 1.0

$controlHelp.Add_Click({
    $dialog = New-Object System.Windows.Forms.Form
    $dialog.Text = 'Keyboard + Mouse Settings'
    $dialog.ClientSize = New-Object System.Drawing.Size(460, 360)
    $dialog.StartPosition = 'CenterParent'; $dialog.FormBorderStyle = 'FixedDialog'
    $dialog.MaximizeBox = $false; $dialog.MinimizeBox = $false
    $dialog.Font = $form.Font; $dialog.BackColor = $form.BackColor; $dialog.ForeColor = $form.ForeColor
    $help = New-Object System.Windows.Forms.Label
    $help.SetBounds(20, 16, 420, 240)
    $help.Text = "WASD: move | Mouse / IJKL: camera`nSpace: A (jump / confirm)`nHold Shift: B (run / dash)`nE: B (interact / back)`nLeft mouse: X (attack) | R: Y (roll)`nHold Q: LT (crouch) | Right mouse: RT (command)`nF: LB (throw mode) | C: RB (ability)`nArrows: D-pad | V: LS click | Middle mouse: RS click`nEnter: pause (Start) | Tab: map / fast-forward (Back)`nEsc: quit"
    $dialog.Controls.Add($help)
    $caption = New-Object System.Windows.Forms.Label
    $caption.Text = 'Mouse sensitivity:'; $caption.SetBounds(20, 270, 140, 24)
    $dialog.Controls.Add($caption)
    $sensitivity = New-Object System.Windows.Forms.NumericUpDown
    $sensitivity.SetBounds(160, 268, 100, 26); $sensitivity.DecimalPlaces = 2
    $sensitivity.Minimum = 0.01; $sensitivity.Maximum = 10; $sensitivity.Increment = 0.1
    $sensitivity.BackColor = $cControlBg; $sensitivity.ForeColor = $cTextMain
    $sensitivity.Value = [decimal]$script:mouseSensitivity
    $dialog.Controls.Add($sensitivity)
    $ok = New-Object System.Windows.Forms.Button
    $ok.Text = 'OK'; $ok.SetBounds(340, 310, 100, 32); $ok.DialogResult = 'OK'
    $ok.FlatStyle = 'Flat'; $ok.BackColor = $cControlBg; $ok.ForeColor = $cTextMain
    $dialog.Controls.Add($ok); $dialog.AcceptButton = $ok
    if ($dialog.ShowDialog($form) -eq 'OK') { $script:mouseSensitivity = [double]$sensitivity.Value }
    $dialog.Dispose()
})

Add-FieldLabel $grpControls 'Game folder:' 16 61 100
$dataPath = New-Object System.Windows.Forms.TextBox
$dataPath.SetBounds(120, 58, 264, 26)
$dataPath.Font = New-Object System.Drawing.Font('Segoe UI', 8.5)
$dataPath.BackColor = $cControlBg
$dataPath.ForeColor = $cTextMain
$dataPath.BorderStyle = 'FixedSingle'
$dataPath.Text = Join-Path $projectRoot 'game-data-usa'
$grpControls.Controls.Add($dataPath)

$browse = New-Object System.Windows.Forms.Button
$browse.Text = 'Browse...'
$browse.SetBounds(392, 58, 82, 26)
$browse.FlatStyle = 'Flat'
$browse.Font = New-Object System.Drawing.Font('Segoe UI', 8.5)
$browse.BackColor = $cControlBg
$browse.ForeColor = $cTextMain
$browse.FlatAppearance.BorderColor = $cBorder
$grpControls.Controls.Add($browse)

$browse.Add_Click({
    $dialog = New-Object System.Windows.Forms.FolderBrowserDialog
    $dialog.Description = 'Select the game folder containing default.xex.'
    $dialog.ShowNewFolderButton = $false
    if (Test-Path -LiteralPath $dataPath.Text -PathType Container) { $dialog.SelectedPath = $dataPath.Text }
    try { if ($dialog.ShowDialog() -eq [System.Windows.Forms.DialogResult]::OK) { $dataPath.Text = $dialog.SelectedPath } }
    finally { $dialog.Dispose() }
})

# Performance recording check
$chkRecordPerf = New-Object System.Windows.Forms.CheckBox
$chkRecordPerf.Text = 'Record session performance CSV log (reports/)'
$chkRecordPerf.SetBounds(16, 96, 458, 22)
$chkRecordPerf.Font = New-Object System.Drawing.Font('Segoe UI', 8.5)
$chkRecordPerf.ForeColor = $cTextMuted
$chkRecordPerf.BackColor = [System.Drawing.Color]::Transparent
$chkRecordPerf.FlatStyle = 'Flat'
$chkRecordPerf.Checked = $true
$grpControls.Controls.Add($chkRecordPerf)

# =========================================================================
# Load Saved Configuration
# =========================================================================
if (Test-Path -LiteralPath $settingsPath) {
    try {
        $saved = Get-Content -LiteralPath $settingsPath -Raw | ConvertFrom-Json
        if ($size.Items.Contains([string]$saved.WindowSize)) { $size.SelectedItem = [string]$saved.WindowSize }
        if ($saved.Fullscreen -is [bool]) { $mode.SelectedIndex = if ($saved.Fullscreen) { 1 } else { 0 } }
        if ($saved.VSync -is [bool]) { $sync.SelectedIndex = if ($saved.VSync) { 1 } else { 0 } }
        if ($saved.RenderScale -in @(1,2,3)) { $scale.SelectedIndex = [int]$saved.RenderScale - 1 }
        if ($has60 -and $saved.FrameRateMode) { $frameRate.SelectedIndex = if ($saved.FrameRateMode -eq 'Original') { 1 } else { 0 } }
        if ($saved.RenderingPath -eq 'ROV') {
            $renderingPath.SelectedIndex = 2
        } elseif ($saved.RenderingPath -eq 'Default' -or $saved.RenderingPath -eq 'RTV') {
            $renderingPath.SelectedIndex = 1
        } else {
            $renderingPath.SelectedIndex = 0
        }
        if ($saved.PostEffect -eq 'none') { $effect.SelectedIndex = 1 } else { $effect.SelectedIndex = 0 }
        $upscalerIdx = @('fsr','cas','bilinear').IndexOf([string]$saved.Upscaler)
        if ($upscalerIdx -ge 0) { $upscaler.SelectedIndex = $upscalerIdx }
        if ($saved.SkipIntroLogos -is [bool]) { $chkSkipLogos.Checked = $saved.SkipIntroLogos }
        if ($saved.SkipCutscenes -is [bool]) { $cutsceneEnabled.Checked = $saved.SkipCutscenes }
        if ($saved.CutsceneSpeed -in @(2,4,6,8)) {
            $speedIdx = @(2,4,6,8).IndexOf([int]$saved.CutsceneSpeed)
            if ($speedIdx -ge 0) { $cutsceneSpeed.SelectedIndex = $speedIdx }
        }
        $cutsceneSpeed.Enabled = $cutsceneEnabled.Checked
        if ($saved.InputMode -eq 'KeyboardMouse') { $inputMode.SelectedIndex = 1 } else { $inputMode.SelectedIndex = 0 }
        if ($saved.MouseSensitivity -is [ValueType] -and $saved.MouseSensitivity -ge 0.01 -and $saved.MouseSensitivity -le 10) {
            $script:mouseSensitivity = [double]$saved.MouseSensitivity
        }
        if ($saved.RecordPerformance -is [bool]) { $chkRecordPerf.Checked = $saved.RecordPerformance }
        if ($saved.GameDataRoot -and (Test-Path -LiteralPath $saved.GameDataRoot)) {
            $dataPath.Text = [string]$saved.GameDataRoot
        }
    } catch {
        # Defaults used
    }
}

# =========================================================================
# Footer Action Buttons
# =========================================================================
$cancel = New-Object System.Windows.Forms.Button
$cancel.Text = 'Exit'
$cancel.SetBounds(20, 608, 120, 46)
$cancel.FlatStyle = 'Flat'
$cancel.Font = New-Object System.Drawing.Font('Segoe UI', 9.5, [System.Drawing.FontStyle]::Bold)
$cancel.BackColor = [System.Drawing.Color]::FromArgb(51, 65, 85)
$cancel.ForeColor = [System.Drawing.Color]::FromArgb(241, 245, 249)
$cancel.FlatAppearance.BorderColor = $cBorder
$cancel.Add_Click({ $form.Close() })
$form.Controls.Add($cancel)
$form.CancelButton = $cancel

$play = New-Object System.Windows.Forms.Button
$play.Text = '▶   LAUNCH GAME'
$play.SetBounds(155, 608, 355, 46)
$play.FlatStyle = 'Flat'
$play.Font = New-Object System.Drawing.Font('Segoe UI', 11.0, [System.Drawing.FontStyle]::Bold)
$play.BackColor = [System.Drawing.Color]::FromArgb(22, 163, 74) # Vibrant emerald
$play.ForeColor = [System.Drawing.Color]::White
$play.FlatAppearance.BorderSize = 0
$form.Controls.Add($play)

$play.Add_Click({
    try {
        $selectedGameData = $dataPath.Text.Trim()
        if (-not (Test-Path -LiteralPath (Join-Path $selectedGameData 'default.xex'))) {
            throw "default.xex was not found in: $selectedGameData"
        }
        $options = @{
            Configuration     = $Configuration
            GameDataRoot      = $selectedGameData
            WindowSize        = [string]$size.SelectedItem
            Fullscreen        = ($mode.SelectedIndex -eq 1)
            VSync             = ($sync.SelectedIndex -eq 1)
            RenderScale       = ($scale.SelectedIndex + 1)
            PostEffect        = (@('fxaa','none')[$effect.SelectedIndex])
            Upscaler          = (@('fsr','cas','bilinear')[$upscaler.SelectedIndex])
            FrameRateMode     = (@('60','Original')[$frameRate.SelectedIndex])
            RenderingPath     = (@('Vulkan','Default','ROV')[$renderingPath.SelectedIndex])
            SkipIntroLogos    = $chkSkipLogos.Checked
            SkipCutscenes     = $cutsceneEnabled.Checked
            CutsceneSpeed     = $speedMap[$cutsceneSpeed.SelectedIndex]
            InputMode         = (@('Controller','KeyboardMouse')[$inputMode.SelectedIndex])
            MouseSensitivity  = $script:mouseSensitivity
            RecordPerformance = $chkRecordPerf.Checked
            IncludeHardware   = $false
        }
        $folder = Split-Path $settingsPath -Parent
        New-Item -ItemType Directory -Force -Path $folder | Out-Null
        $tempPath = "$settingsPath.tmp"
        $options | ConvertTo-Json | Set-Content -LiteralPath $tempPath -Encoding UTF8
        Move-Item -LiteralPath $tempPath -Destination $settingsPath -Force
        $form.Hide()
        & (Join-Path $PSScriptRoot 'run-usa.ps1') @options
        $form.Close()
    } catch {
        $form.Show()
        [System.Windows.Forms.MessageBox]::Show($_.Exception.Message, 'Unable to launch game') | Out-Null
    }
})

# =========================================================================
# Validation / Display Mode
# =========================================================================
if ($ValidateOnly) {
    $form.ShowInTaskbar = $false
    $form.StartPosition = 'Manual'
    $form.Location = New-Object System.Drawing.Point(-10000,-10000)
    $form.Show()
    $form.CreateControl()
    $form.PerformLayout()
    $bitmap = New-Object System.Drawing.Bitmap($form.Width, $form.Height)
    $form.DrawToBitmap($bitmap, (New-Object System.Drawing.Rectangle(0,0,$form.Width,$form.Height)))
    $bitmap.Save((Join-Path $projectRoot 'reports\launcher-preview.png'))
    $bitmap.Dispose()
    Write-Output 'Majistic Launcher validated.'
} else {
    [void]$form.ShowDialog()
}

$tooltip.Dispose()
$form.Dispose()
