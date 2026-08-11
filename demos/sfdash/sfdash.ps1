param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('map', 'dossier', 'drop', 'radar', 'cipher', 'bio', 'spectrum', 'threat', 'network', 'clock')]
    [string]$Mode,

    [ValidateRange(100, 10000)]
    [int]$RefreshMilliseconds = 1000,

    [ValidateRange(0, 1000000)]
    [int]$Frames = 0
)

$esc = [char]27
$cyan = "$esc[38;2;40;235;255m"
$blue = "$esc[38;2;40;120;255m"
$green = "$esc[38;2;80;255;150m"
$amber = "$esc[38;2;255;190;60m"
$red = "$esc[38;2;255;70;90m"
$dim = "$esc[38;2;65;110;125m"
$white = "$esc[38;2;210;245;255m"
$reset = "$esc[0m"
$bold = "$esc[1m"
$rng = [System.Random]::new()

function Get-DashboardWidth {
    try { return [Math]::Max(24, [Console]::WindowWidth - 1) } catch { return 48 }
}

function Get-DashboardHeight {
    try { return [Math]::Max(8, [Console]::WindowHeight - 1) } catch { return 16 }
}

function Get-PlainLength([string]$Text) {
    return ($Text -replace '\x1B\[[0-?]*[ -/]*[@-~]', '').Length
}

function New-Meter([int]$Value, [int]$Maximum, [int]$Size) {
    $filled = [Math]::Min($Size, [Math]::Max(0, [int]($Value * $Size / $Maximum)))
    return ('█' * $filled) + ('░' * ($Size - $filled))
}

function New-Rule([int]$Width, [string]$Glyph = '─') {
    return $Glyph * [Math]::Max(1, $Width)
}

function New-Header([string]$Title, [string]$Tag, [int]$Width) {
    $fill = [Math]::Max(1, $Width - $Title.Length - $Tag.Length - 5)
    return "$bold$cyan$Title$reset $dim$(New-Rule $fill)$reset $green$Tag$reset"
}

function New-Canvas([int]$Width, [int]$Height, [char]$Fill = ' ') {
    $canvas = [char[,]]::new($Height, $Width)
    for ($y = 0; $y -lt $Height; $y++) {
        for ($x = 0; $x -lt $Width; $x++) { $canvas[$y, $x] = $Fill }
    }
    return ,$canvas
}

function Set-Pixel($Canvas, [int]$X, [int]$Y, [char]$Glyph) {
    $height = $Canvas.GetLength(0)
    $width = $Canvas.GetLength(1)
    if ($X -ge 0 -and $X -lt $width -and $Y -ge 0 -and $Y -lt $height) {
        $Canvas[$Y, $X] = $Glyph
    }
}

function Set-CanvasText($Canvas, [int]$X, [int]$Y, [string]$Text) {
    for ($i = 0; $i -lt $Text.Length; $i++) { Set-Pixel $Canvas ($X + $i) $Y $Text[$i] }
}

function Add-CanvasLine($Canvas, [int]$X0, [int]$Y0, [int]$X1, [int]$Y1, [char]$Glyph = '·') {
    $dx = [Math]::Abs($X1 - $X0)
    $sx = if ($X0 -lt $X1) { 1 } else { -1 }
    $dy = -[Math]::Abs($Y1 - $Y0)
    $sy = if ($Y0 -lt $Y1) { 1 } else { -1 }
    $errorValue = $dx + $dy
    while ($true) {
        Set-Pixel $Canvas $X0 $Y0 $Glyph
        if ($X0 -eq $X1 -and $Y0 -eq $Y1) { break }
        $twiceError = 2 * $errorValue
        if ($twiceError -ge $dy) { $errorValue += $dy; $X0 += $sx }
        if ($twiceError -le $dx) { $errorValue += $dx; $Y0 += $sy }
    }
}

function Convert-CanvasToLines($Canvas, [string]$Color) {
    $height = $Canvas.GetLength(0)
    $width = $Canvas.GetLength(1)
    $rows = [System.Collections.Generic.List[string]]::new()
    for ($y = 0; $y -lt $height; $y++) {
        $builder = [System.Text.StringBuilder]::new($width)
        for ($x = 0; $x -lt $width; $x++) { [void]$builder.Append($Canvas[$y, $x]) }
        $rows.Add("$Color$($builder.ToString())$reset")
    }
    return $rows
}

function Write-Frame([System.Collections.Generic.List[string]]$Lines, [int]$Width, [int]$Height) {
    $output = [System.Text.StringBuilder]::new()
    [void]$output.Append("$esc[H")
    for ($i = 0; $i -lt $Height; $i++) {
        $line = if ($i -lt $Lines.Count) { $Lines[$i] } else { '' }
        $plainLength = Get-PlainLength $line
        [void]$output.Append("$esc[2K$line")
        if ($plainLength -lt $Width) { [void]$output.Append(' ' * ($Width - $plainLength)) }
        if ($i -lt ($Height - 1)) { [void]$output.Append("`n") }
    }
    Write-Host $output.ToString() -NoNewline
}

function Add-MapView($Lines, [int]$Width, [int]$BodyHeight, [int]$Tick) {
    $canvas = New-Canvas $Width $BodyHeight ' '
    for ($x = 0; $x -lt $Width; $x++) {
        Set-Pixel $canvas $x 0 '─'; Set-Pixel $canvas $x ($BodyHeight - 1) '─'
    }
    for ($y = 0; $y -lt $BodyHeight; $y++) {
        Set-Pixel $canvas 0 $y '│'; Set-Pixel $canvas ($Width - 1) $y '│'
    }
    for ($x = 7; $x -lt ($Width - 1); $x += 8) {
        for ($y = 1; $y -lt ($BodyHeight - 1); $y++) { Set-Pixel $canvas $x $y '┊' }
    }
    for ($y = 3; $y -lt ($BodyHeight - 1); $y += 4) {
        for ($x = 1; $x -lt ($Width - 1); $x++) { Set-Pixel $canvas $x $y '╌' }
    }
    $assetX = 2 + ($Tick % [Math]::Max(1, $Width - 4))
    $assetY = 1 + ([int]($Tick / 3) % [Math]::Max(1, $BodyHeight - 2))
    Set-Pixel $canvas $assetX $assetY '●'
    Set-Pixel $canvas ([Math]::Max(2, [int]($Width * 0.22))) ([Math]::Max(1, [int]($BodyHeight * 0.32))) '◆'
    Set-Pixel $canvas ([Math]::Max(2, [int]($Width * 0.76))) ([Math]::Max(1, [int]($BodyHeight * 0.68))) '◆'
    if ($Width -gt 42 -and $BodyHeight -gt 4) {
        Set-CanvasText $canvas 2 1 'SECTOR 7G // MITTE'
        Set-CanvasText $canvas ([Math]::Max(2, $Width - 19)) ($BodyHeight - 2) '52.520N 13.405E'
    }
    foreach ($row in (Convert-CanvasToLines $canvas $dim)) { $Lines.Add($row) }
}

function Add-DossierView($Lines, [int]$Width, [int]$BodyHeight, [int]$Tick) {
    $aliases = @('NIGHTJAR', 'SABLE', 'VECTOR', 'KITE')
    $alias = $aliases[[int]($Tick / 18) % $aliases.Count]
    $fields = @(
        "CODENAME      $alias",
        'CLEARANCE     OMEGA / ULTRAVIOLET',
        'STATUS        ACTIVE // CLEAN',
        "LAST SIGNAL   $((Get-Date).ToUniversalTime().ToString('HH:mm:ss')) ZULU",
        'IDENTITY      ███████ ███████████',
        "HANDLER       CONTROL-$(100 + ($Tick % 899))",
        'SAFEHOUSE     ████-██ / BERLIN',
        'BIOMETRIC     MATCH 99.97%',
        'COMMS         BURST CHANNEL 07',
        'EXFIL         WINDOW CONFIRMED'
    )
    for ($row = 0; $row -lt $BodyHeight; $row++) {
        $field = $fields[$row % $fields.Count]
        $prefix = if ($row -eq 0) { '▶ ' } elseif ($row % 3 -eq 0) { '◆ ' } else { '│ ' }
        $fill = [Math]::Max(1, $Width - $prefix.Length - $field.Length - 1)
        $color = if ($row -eq 0) { $cyan } elseif ($row % 4 -eq 3) { $amber } else { $white }
        $Lines.Add("$dim$prefix$reset$color$field$reset$dim $('·' * $fill)$reset")
    }
}

function Add-DropView($Lines, [int]$Width, [int]$BodyHeight, [int]$Tick) {
    $states = @('ENCRYPTING', 'STAGED', 'IN TRANSIT', 'DELIVERED')
    $barWidth = [Math]::Max(5, $Width - 31)
    for ($row = 0; $row -lt $BodyHeight; $row++) {
        $percent = ($Tick * 3 + $row * 17) % 101
        $state = $states[($row + [int]($Tick / 10)) % $states.Count]
        $color = if ($state -eq 'DELIVERED') { $green } elseif ($state -eq 'IN TRANSIT') { $amber } else { $cyan }
        $Lines.Add(("$dim PKT-{0:D2} ┊$reset {1}{2,-10}$reset {3} {4,3}%" -f ($row + 11), $color, $state, (New-Meter $percent 100 $barWidth), $percent))
    }
}

function Add-RadarView($Lines, [int]$Width, [int]$BodyHeight, [int]$Tick) {
    $canvas = New-Canvas $Width $BodyHeight ' '
    $centerX = [int]([Math]::Min($Width * 0.42, $Width / 2))
    $centerY = [int](($BodyHeight - 1) / 2)
    $radius = [Math]::Max(2, [Math]::Min($centerX * 0.48, ($BodyHeight - 1) / 2.0))
    $angle = $Tick * 0.20
    for ($y = 0; $y -lt $BodyHeight; $y++) {
        for ($x = 0; $x -lt $Width; $x++) {
            $dx = ($x - $centerX) / 2.0
            $dy = $y - $centerY
            $distance = [Math]::Sqrt($dx * $dx + $dy * $dy)
            $pointAngle = [Math]::Atan2($dy, $dx)
            $delta = [Math]::Abs([Math]::Atan2([Math]::Sin($pointAngle - $angle), [Math]::Cos($pointAngle - $angle)))
            if ([Math]::Abs($distance - $radius) -lt 0.30 -or [Math]::Abs($distance - ($radius * 0.66)) -lt 0.22 -or [Math]::Abs($distance - ($radius * 0.33)) -lt 0.18) {
                Set-Pixel $canvas $x $y '·'
            }
            if ($delta -lt 0.07 -and $distance -le $radius) { Set-Pixel $canvas $x $y '━' }
        }
    }
    Set-Pixel $canvas $centerX $centerY '⊕'
    Set-Pixel $canvas ([int]($centerX + $radius)) ([Math]::Max(0, $centerY - 2)) '◆'
    Set-Pixel $canvas ([int]($centerX - $radius * 1.4)) ([Math]::Min($BodyHeight - 1, $centerY + 2)) '◆'
    if ($Width -gt 42) {
        $panelX = [Math]::Max($centerX + [int]($radius * 2) + 3, [int]($Width * 0.63))
        Set-CanvasText $canvas $panelX 1 'CONTACT 02'
        if ($BodyHeight -gt 3) { Set-CanvasText $canvas $panelX 3 'BRG  071.4°' }
        if ($BodyHeight -gt 5) { Set-CanvasText $canvas $panelX 5 'ALT  420 KM' }
        if ($BodyHeight -gt 7) { Set-CanvasText $canvas $panelX 7 'VEL  7.6 KM/S' }
    }
    foreach ($row in (Convert-CanvasToLines $canvas $dim)) { $Lines.Add($row) }
}

function Add-CipherView($Lines, [int]$Width, [int]$BodyHeight, [int]$Tick) {
    $alphabet = '01ABCDEF∆◇⌁'
    for ($y = 0; $y -lt $BodyHeight; $y++) {
        $builder = [System.Text.StringBuilder]::new()
        for ($x = 0; $x -lt $Width; $x++) {
            $glyph = $alphabet[$rng.Next(0, $alphabet.Length)]
            if (($x + $y * 7 + $Tick) % 19 -eq 0) {
                [void]$builder.Append("$white$glyph$reset$dim")
            } elseif (($x * 3 + $Tick) % 23 -eq 0) {
                [void]$builder.Append("$cyan$glyph$reset$dim")
            } else {
                [void]$builder.Append($glyph)
            }
        }
        $Lines.Add("$dim$($builder.ToString())$reset")
    }
}

function Add-BiometricView($Lines, [int]$Width, [int]$BodyHeight, [int]$Tick) {
    $canvas = New-Canvas $Width $BodyHeight '·'
    $centerX = [int]($Width / 2)
    $centerY = [int]($BodyHeight / 2)
    $radiusX = [Math]::Max(4, [int]($Width * 0.28))
    $radiusY = [Math]::Max(2, [int]($BodyHeight * 0.36))
    for ($y = 0; $y -lt $BodyHeight; $y++) {
        for ($x = 0; $x -lt $Width; $x++) {
            $ellipse = [Math]::Pow(($x - $centerX) / $radiusX, 2) + [Math]::Pow(($y - $centerY) / $radiusY, 2)
            if ([Math]::Abs($ellipse - 1.0) -lt 0.20) { Set-Pixel $canvas $x $y '◦' }
        }
    }
    $scanY = $Tick % [Math]::Max(1, $BodyHeight)
    for ($x = 0; $x -lt $Width; $x++) { Set-Pixel $canvas $x $scanY '━' }
    Set-Pixel $canvas $centerX $centerY '◎'
    if ($BodyHeight -gt 2) { Set-CanvasText $canvas 2 1 "HEART $(68 + ($Tick % 7)) BPM" }
    if ($BodyHeight -gt 4) { Set-CanvasText $canvas 2 ($BodyHeight - 2) 'RETINA 99.97% // VOICE 98.42%' }
    foreach ($row in (Convert-CanvasToLines $canvas $cyan)) { $Lines.Add($row) }
}

function Add-SpectrumView($Lines, [int]$Width, [int]$BodyHeight) {
    $graphHeight = [Math]::Max(1, $BodyHeight - 1)
    $values = for ($x = 0; $x -lt $Width; $x++) { 1 + $rng.Next($graphHeight) }
    for ($y = $graphHeight; $y -ge 1; $y--) {
        $builder = [System.Text.StringBuilder]::new()
        for ($x = 0; $x -lt $Width; $x++) {
            if ($values[$x] -ge $y) {
                $color = if ($y -ge ($graphHeight * 0.80)) { $red } elseif ($y -ge ($graphHeight * 0.55)) { $amber } else { $cyan }
                [void]$builder.Append("$color▇$reset")
            } else { [void]$builder.Append(' ') }
        }
        $Lines.Add($builder.ToString())
    }
    $label = '88MHz     104       121       144       168       192MHz'
    $Lines.Add("$dim$($label.Substring(0, [Math]::Min($label.Length, $Width)))$reset")
}

function Add-ThreatView($Lines, [int]$Width, [int]$BodyHeight, [int]$Tick) {
    $zones = @('NORTH ATLANTIC', 'BALTIC GRID', 'LEVANT NODE', 'PACIFIC ARC', 'BLACK SEA', 'ARCTIC RING', 'SAHEL RELAY', 'ANDES LINK', 'INDIAN OCEAN', 'LUNAR NET')
    $barWidth = [Math]::Max(4, $Width - 29)
    for ($row = 0; $row -lt $BodyHeight; $row++) {
        $zone = $zones[$row % $zones.Count]
        $value = ($Tick * 7 + $row * 23) % 100
        $color = if ($value -gt 75) { $red } elseif ($value -gt 45) { $amber } else { $green }
        $glyph = if ($value -gt 75) { '▲' } elseif ($value -gt 45) { '◆' } else { '●' }
        $Lines.Add(("$dim {0,-14}$reset {1}{2}$reset {3} {4,2}" -f $zone, $color, $glyph, (New-Meter $value 100 $barWidth), $value))
    }
}

function Add-NetworkView($Lines, [int]$Width, [int]$BodyHeight, [int]$Tick) {
    $canvas = New-Canvas $Width $BodyHeight ' '
    for ($y = 0; $y -lt $BodyHeight; $y += 2) {
        for ($x = ($y % 4); $x -lt $Width; $x += 4) { Set-Pixel $canvas $x $y '·' }
    }
    $nodes = @(
        @([int]($Width * 0.50), [int]($BodyHeight * 0.12), 'HQ'),
        @([int]($Width * 0.16), [int]($BodyHeight * 0.42), 'A7'),
        @([int]($Width * 0.50), [int]($BodyHeight * 0.50), 'RLY'),
        @([int]($Width * 0.82), [int]($BodyHeight * 0.35), 'B2'),
        @([int]($Width * 0.28), [int]($BodyHeight * 0.82), 'X1'),
        @([int]($Width * 0.72), [int]($BodyHeight * 0.84), 'EXIT')
    )
    foreach ($edge in @(@(0,1), @(0,2), @(0,3), @(1,4), @(2,4), @(2,5), @(3,5), @(4,5))) {
        Add-CanvasLine $canvas $nodes[$edge[0]][0] $nodes[$edge[0]][1] $nodes[$edge[1]][0] $nodes[$edge[1]][1] '·'
    }
    foreach ($node in $nodes) {
        Set-Pixel $canvas $node[0] $node[1] '◆'
        Set-CanvasText $canvas ($node[0] + 2) $node[1] $node[2]
    }
    $packetEdge = $Tick % 8
    $from = $nodes[@(0,0,0,1,2,2,3,4)[$packetEdge]]
    $to = $nodes[@(1,2,3,4,4,5,5,5)[$packetEdge]]
    $phase = ($Tick % 10) / 10.0
    $packetX = [int]($from[0] + ($to[0] - $from[0]) * $phase)
    $packetY = [int]($from[1] + ($to[1] - $from[1]) * $phase)
    Set-Pixel $canvas $packetX $packetY '●'
    foreach ($row in (Convert-CanvasToLines $canvas $blue)) { $Lines.Add($row) }
}

function Add-ClockView($Lines, [int]$Width, [int]$BodyHeight, [int]$Tick) {
    $font = @{
        '0' = @('███', '█ █', '█ █', '█ █', '███'); '1' = @(' ██', '  █', '  █', '  █', '███')
        '2' = @('███', '  █', '███', '█  ', '███'); '3' = @('███', '  █', ' ██', '  █', '███')
        '4' = @('█ █', '█ █', '███', '  █', '  █'); '5' = @('███', '█  ', '███', '  █', '███')
        '6' = @('███', '█  ', '███', '█ █', '███'); '7' = @('███', '  █', '  █', '  █', '  █')
        '8' = @('███', '█ █', '███', '█ █', '███'); '9' = @('███', '█ █', '███', '  █', '███')
        ':' = @(' ', '●', ' ', '●', ' ')
    }
    $now = (Get-Date).ToUniversalTime()
    $timeText = $now.ToString('HH:mm:ss')
    $bigWidth = ($timeText.Length * 4) - 1
    $left = [Math]::Max(0, [int](($Width - $bigWidth) / 2))
    for ($fontRow = 0; $fontRow -lt [Math]::Min(5, $BodyHeight); $fontRow++) {
        $text = ' ' * $left
        foreach ($character in $timeText.ToCharArray()) { $text += $font[[string]$character][$fontRow] + ' ' }
        $Lines.Add("$cyan$text$reset")
    }
    $details = @(
        "ZULU      $($now.ToString('HH:mm:ss'))",
        "LONDON    $((Get-Date).ToString('HH:mm:ss'))",
        "NEW YORK  $($now.AddHours(-4).ToString('HH:mm:ss'))",
        "TOKYO     $($now.AddHours(9).ToString('HH:mm:ss'))",
        "WINDOW    T-$([TimeSpan]::FromSeconds(86400 - ([int]$now.TimeOfDay.TotalSeconds % 86400)).ToString('hh\:mm\:ss'))",
        "UPLINK    $(New-Meter (($Tick * 7) % 100) 100 ([Math]::Max(5, $Width - 22)))",
        'COMMS     SECURE // BURST-7',
        'STATUS    ARMED // NO ABORT'
    )
    while ($Lines.Count -lt $BodyHeight) {
        $index = ($Lines.Count - 5) % $details.Count
        if ($index -lt 0) { $index = 0 }
        $Lines.Add("$dim$($details[$index])$reset")
    }
}

[Console]::OutputEncoding = [System.Text.UTF8Encoding]::new()
Write-Host "$esc[2J$esc[H$esc[?25l" -NoNewline
$tick = 0

try {
    while ($true) {
        $tick++
        $width = Get-DashboardWidth
        $height = Get-DashboardHeight
        $bodyHeight = [Math]::Max(1, $height - 2)
        $lines = [System.Collections.Generic.List[string]]::new()

        $title = switch ($Mode) {
            'map' { 'FIELD MAP // BERLIN' } 'dossier' { 'ASSET DOSSIER' } 'drop' { 'DEAD DROP // QUEUE' }
            'radar' { 'ORBITAL RADAR' } 'cipher' { 'CIPHER STREAM' } 'bio' { 'BIOMETRIC GRID' }
            'spectrum' { 'COMMS SPECTRUM' } 'threat' { 'THREAT MATRIX' } 'network' { 'NETWORK TRACE' }
            'clock' { 'MISSION CLOCK' }
        }
        $tag = switch ($Mode) {
            'map' { 'LIVE' } 'dossier' { 'EYES ONLY' } 'drop' { 'ARMED' } 'radar' { 'SWEEP' }
            'cipher' { 'AES-256' } 'bio' { 'LOCKED' } 'spectrum' { 'CH 7' } 'threat' { 'DEFCON 3' }
            'network' { 'DARKNET' } 'clock' { 'T-00' }
        }
        $lines.Add((New-Header $title $tag $width))

        switch ($Mode) {
            'map' { Add-MapView $lines $width $bodyHeight $tick }
            'dossier' { Add-DossierView $lines $width $bodyHeight $tick }
            'drop' { Add-DropView $lines $width $bodyHeight $tick }
            'radar' { Add-RadarView $lines $width $bodyHeight $tick }
            'cipher' { Add-CipherView $lines $width $bodyHeight $tick }
            'bio' { Add-BiometricView $lines $width $bodyHeight $tick }
            'spectrum' { Add-SpectrumView $lines $width $bodyHeight }
            'threat' { Add-ThreatView $lines $width $bodyHeight $tick }
            'network' { Add-NetworkView $lines $width $bodyHeight $tick }
            'clock' { Add-ClockView $lines $width $bodyHeight $tick }
        }

        $footer = switch ($Mode) {
            'map' { '● ASSET-7   ◆ DROP   GPS 52.520°N 13.405°E' }
            'dossier' { 'COMPARTMENTALIZED // DO NOT COPY' }
            'drop' { 'ONION ROUTE 7/7 // RECEIPTS SEALED' }
            'radar' { '02 CONTACTS // ALT 420KM // VEL 7.6KM·S⁻¹' }
            'cipher' { "KEY MATCH // SHA3 $([Convert]::ToString(($tick * 7919) % 65535, 16).PadLeft(4, '0').ToUpper())…" }
            'bio' { 'IDENTITY CONFIRMED // LIVENESS PASS' }
            'spectrum' { 'SIGNAL ACQUIRED // -42 dBm // SNR 31.8' }
            'threat' { '▲ CRITICAL   ◆ WATCH   ● CLEAR   // PREDICT-9' }
            'network' { "7 HOPS // LAT $(23 + $tick % 19)ms // LOSS 0.0% // ROUTE MASKED" }
            'clock' { '● UPLINK   ● COMMS   ● ARMED   // NO SECOND CHANCES' }
        }
        $lines.Add("$green$footer$reset")
        Write-Frame $lines $width $height
        if ($Frames -gt 0 -and $tick -ge $Frames) { break }
        Start-Sleep -Milliseconds $RefreshMilliseconds
    }
}
finally {
    Write-Host "$esc[?25h$reset"
}
