# PowerShell script to configure Windows Firewall for Mini2 gRPC ports
# Run this as Administrator on both Windows PCs

Write-Host "=== Mini2 Windows Firewall Setup ===" -ForegroundColor Cyan
Write-Host ""

# Check if running as Administrator
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $isAdmin) {
    Write-Host "ERROR: This script must be run as Administrator!" -ForegroundColor Red
    Write-Host "Right-click PowerShell and select 'Run as Administrator'" -ForegroundColor Yellow
    exit 1
}

Write-Host "✓ Running as Administrator" -ForegroundColor Green
Write-Host ""

# Define the ports
$ports = 50050..50055
$ruleName = "Mini2-gRPC-Ports"

# Remove existing rules if any
Write-Host "Removing existing firewall rules (if any)..." -ForegroundColor Yellow
Get-NetFirewallRule -DisplayName "$ruleName*" -ErrorAction SilentlyContinue | Remove-NetFirewallRule

# Create inbound rule for all ports
Write-Host "Creating inbound firewall rule for ports 50050-50055..." -ForegroundColor Yellow
try {
    New-NetFirewallRule `
        -DisplayName "$ruleName-Inbound" `
        -Direction Inbound `
        -Protocol TCP `
        -LocalPort 50050-50055 `
        -Action Allow `
        -Profile Any `
        -Enabled True `
        -Description "Allow inbound gRPC connections for Mini2 distributed system"
    
    Write-Host "✓ Inbound rule created successfully" -ForegroundColor Green
} catch {
    Write-Host "✗ Failed to create inbound rule: $_" -ForegroundColor Red
    exit 1
}

# Create outbound rule for all ports
Write-Host "Creating outbound firewall rule for ports 50050-50055..." -ForegroundColor Yellow
try {
    New-NetFirewallRule `
        -DisplayName "$ruleName-Outbound" `
        -Direction Outbound `
        -Protocol TCP `
        -RemotePort 50050-50055 `
        -Action Allow `
        -Profile Any `
        -Enabled True `
        -Description "Allow outbound gRPC connections for Mini2 distributed system"
    
    Write-Host "✓ Outbound rule created successfully" -ForegroundColor Green
} catch {
    Write-Host "✗ Failed to create outbound rule: $_" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "=== Firewall Configuration Complete ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "You can verify the rules with:" -ForegroundColor Yellow
Write-Host "  Get-NetFirewallRule -DisplayName 'Mini2-gRPC-Ports*' | Format-List" -ForegroundColor Gray
Write-Host ""
Write-Host "To remove these rules later:" -ForegroundColor Yellow
Write-Host "  Get-NetFirewallRule -DisplayName 'Mini2-gRPC-Ports*' | Remove-NetFirewallRule" -ForegroundColor Gray
Write-Host ""

# Test current network connectivity
Write-Host "Current network interfaces:" -ForegroundColor Cyan
Get-NetIPAddress -AddressFamily IPv4 | Where-Object {$_.IPAddress -notlike "127.*"} | Select-Object IPAddress, InterfaceAlias | Format-Table -AutoSize

Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "  1. Run this script on BOTH Windows PCs" -ForegroundColor White
Write-Host "  2. Ensure both PCs are on the same network" -ForegroundColor White
Write-Host "  3. Test connectivity: ping <other-pc-ip>" -ForegroundColor White
Write-Host "  4. Start servers in WSL: ./scripts/start_servers.sh" -ForegroundColor White
Write-Host "  5. Run diagnostic: ./scripts/diagnose_network.sh" -ForegroundColor White
