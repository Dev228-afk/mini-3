# ==============================================================================
# WINDOWS FIREWALL FIX FOR MINI-2 CROSS-COMPUTER COMMUNICATION
# ==============================================================================
# Run this script in PowerShell AS ADMINISTRATOR on BOTH computers
# ==============================================================================

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "MINI-2 WINDOWS FIREWALL FIX" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""

# Step 1: Remove old rules if they exist
Write-Host "Step 1: Removing old firewall rules..." -ForegroundColor Yellow
$ruleName = "Mini2_gRPC_Servers"
$existingRules = Get-NetFirewallRule -DisplayName $ruleName -ErrorAction SilentlyContinue
if ($existingRules) {
    Remove-NetFirewallRule -DisplayName $ruleName
    Write-Host "  Old rules removed" -ForegroundColor Green
} else {
    Write-Host "  No old rules found" -ForegroundColor Gray
}

# Step 2: Create comprehensive inbound rules for all ports
Write-Host ""
Write-Host "Step 2: Creating inbound firewall rules..." -ForegroundColor Yellow

# TCP Inbound - Allow connections TO our servers
New-NetFirewallRule -DisplayName "$ruleName-TCP-In" `
    -Direction Inbound `
    -Protocol TCP `
    -LocalPort 50050-50055 `
    -Action Allow `
    -Profile Any `
    -Enabled True `
    -Description "Allow inbound TCP connections to Mini-2 gRPC servers (ports 50050-50055)"

Write-Host "  TCP Inbound rule created (ports 50050-50055)" -ForegroundColor Green

# Step 3: Create outbound rules for connections to other computer
Write-Host ""
Write-Host "Step 3: Creating outbound firewall rules..." -ForegroundColor Yellow

# TCP Outbound - Allow connections FROM our servers to remote
New-NetFirewallRule -DisplayName "$ruleName-TCP-Out" `
    -Direction Outbound `
    -Protocol TCP `
    -RemotePort 50050-50055 `
    -Action Allow `
    -Profile Any `
    -Enabled True `
    -Description "Allow outbound TCP connections from Mini-2 gRPC servers to remote (ports 50050-50055)"

Write-Host "  TCP Outbound rule created (ports 50050-50055)" -ForegroundColor Green

# Step 4: Create WSL-specific rules
Write-Host ""
Write-Host "Step 4: Creating WSL-specific rules..." -ForegroundColor Yellow

# Allow WSL to communicate with Windows
New-NetFirewallRule -DisplayName "$ruleName-WSL" `
    -Direction Inbound `
    -Protocol TCP `
    -LocalPort 50050-50055 `
    -Action Allow `
    -Profile Any `
    -InterfaceAlias "vEthernet (WSL)" `
    -Enabled True `
    -Description "Allow WSL to Windows communication for Mini-2"

Write-Host "  WSL interface rule created" -ForegroundColor Green

# Step 5: Check firewall status
Write-Host ""
Write-Host "Step 5: Checking firewall status..." -ForegroundColor Yellow
$firewallStatus = Get-NetFirewallProfile -Profile Private | Select-Object -ExpandProperty Enabled

if ($firewallStatus) {
    Write-Host "  Private firewall is ENABLED" -ForegroundColor Yellow
    Write-Host "  If issues persist, temporarily disable with:" -ForegroundColor Yellow
    Write-Host "    Set-NetFirewallProfile -Profile Private -Enabled False" -ForegroundColor Cyan
} else {
    Write-Host "  Private firewall already disabled" -ForegroundColor Green
}

# Step 6: Verify rules were created
Write-Host ""
Write-Host "Step 6: Verifying rules..." -ForegroundColor Yellow
$rules = Get-NetFirewallRule -DisplayName "$ruleName*"
Write-Host "  Created $($rules.Count) firewall rules" -ForegroundColor Green

# Step 7: Show current listening ports
Write-Host ""
Write-Host "Step 7: Current listening ports..." -ForegroundColor Yellow
$listening = netstat -an | Select-String ":5005[0-5].*LISTENING"
if ($listening) {
    Write-Host $listening -ForegroundColor Gray
} else {
    Write-Host "  No servers listening on ports 50050-50055" -ForegroundColor Yellow
    Write-Host "  Make sure WSL servers are running!" -ForegroundColor Yellow
}

# Step 8: Test connectivity
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "TESTING CONNECTIVITY" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

# Determine which computer this is
$ethernet = Get-NetIPAddress -AddressFamily IPv4 -InterfaceAlias "Ethernet*" -ErrorAction SilentlyContinue | Select-Object -First 1
$myIP = $ethernet.IPAddress

Write-Host "Your IP: $myIP" -ForegroundColor Cyan

if ($myIP -like "192.168.137.189") {
    Write-Host "This is Computer 1" -ForegroundColor Green
    Write-Host ""
    Write-Host "Testing connection to Computer 2 (192.168.137.1)..." -ForegroundColor Yellow
    
    foreach ($port in 50052, 50054, 50055) {
        $result = Test-NetConnection -ComputerName "192.168.137.1" -Port $port -InformationLevel Quiet -WarningAction SilentlyContinue
        if ($result) {
            Write-Host "  Port $port reachable" -ForegroundColor Green
        } else {
            Write-Host "  Port $port NOT reachable" -ForegroundColor Red
        }
    }
} elseif ($myIP -like "192.168.137.1") {
    Write-Host "This is Computer 2" -ForegroundColor Green
    Write-Host ""
    Write-Host "Testing connection to Computer 1 (192.168.137.189)..." -ForegroundColor Yellow
    
    foreach ($port in 50050, 50051, 50053) {
        $result = Test-NetConnection -ComputerName "192.168.137.189" -Port $port -InformationLevel Quiet -WarningAction SilentlyContinue
        if ($result) {
            Write-Host "  Port $port reachable" -ForegroundColor Green
        } else {
            Write-Host "  Port $port NOT reachable" -ForegroundColor Red
        }
    }
} else {
    Write-Host "  Could not determine computer (IP: $myIP)" -ForegroundColor Yellow
}

Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "DONE!" -ForegroundColor Green
Write-Host "========================================" -ForegroundColor Cyan
Write-Host ""
Write-Host "If connectivity still fails, try:" -ForegroundColor Yellow
Write-Host "  1. Restart WSL servers in Ubuntu" -ForegroundColor Cyan
Write-Host "  2. Temporarily disable firewall:" -ForegroundColor Cyan
Write-Host "     Set-NetFirewallProfile -Profile Private -Enabled False" -ForegroundColor Gray
Write-Host "  3. Check network cable connection" -ForegroundColor Cyan
Write-Host ""
