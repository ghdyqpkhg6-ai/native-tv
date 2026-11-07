# Fork tool: copy the prepared gallery (prepare-wallpapers.py's output) to the TV over SSH.
#
# The files go to /var/lib/tvweb/home-wallpaper-uploads/gallery.new and are swapped in for the
# gallery in one rename, so Native Tv never reads a half-copied gallery. The server sees the new
# revision on its next status read and Home picks it up by itself. Nothing else on the TV changes.
#
#   powershell -File fork-tools\sync-wallpapers.ps1 [-Source <walls-tv>] [-Tv <ip>] [-Key <ssh key>]
param(
    [string]$Source = "C:\Users\Vian\Documents\Native TV\walls-tv",
    [string]$Tv = "192.168.50.183",
    [string]$Key = "C:\Users\Vian\.ssh\NanoCell86_webos"
)
$ErrorActionPreference = 'Stop'
$files = Get-ChildItem -LiteralPath $Source -File | Where-Object { $_.Name -match '^(g\d{1,3}(-fit|-blur|-thumb)?\.jpg|gallery\.json)$' }
if (-not ($files | Where-Object Name -eq 'gallery.json')) { throw "No gallery.json in ${Source} - run prepare-wallpapers.py first." }
$megabytes = [math]::Round((($files | Measure-Object Length -Sum).Sum) / 1MB, 1)
$base = '/var/lib/tvweb/home-wallpaper-uploads'
$ssh = @('-i', $Key, '-o', 'BatchMode=yes', "root@$Tv")

# Room for the new set beside the old one, plus a margin.
$free = (ssh @ssh "mkdir -p $base && df -k $base | tail -1 | awk '{print `$4}'").Trim()
if ([int64]$free -lt ([int64]($megabytes * 1024) + 50 * 1024)) { throw "Not enough free space on the TV ($free KB free, $megabytes MB needed)." }

ssh @ssh "rm -rf $base/gallery.new && mkdir -p $base/gallery.new"
scp -O -q -i $Key -o BatchMode=yes @($files.FullName) "root@${Tv}:$base/gallery.new/"
$count = (ssh @ssh "ls $base/gallery.new | wc -l").Trim()
if ([int]$count -ne $files.Count) { throw "Only $count of $($files.Count) files arrived; the old gallery is unchanged." }
ssh @ssh "cd $base && chmod 600 gallery.new/* && rm -rf gallery.old && { [ ! -d gallery ] || mv gallery gallery.old; } && mv gallery.new gallery && rm -rf gallery.old"
Write-Output "Copied $($files.Count) files ($megabytes MB) to $base/gallery on $Tv."
