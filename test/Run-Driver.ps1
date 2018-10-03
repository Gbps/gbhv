function IsAdministrator
{
    $Identity = [System.Security.Principal.WindowsIdentity]::GetCurrent()
    $Principal = New-Object System.Security.Principal.WindowsPrincipal($Identity)
    $Principal.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)
}


function IsUacEnabled
{
    (Get-ItemProperty HKLM:\Software\Microsoft\Windows\CurrentVersion\Policies\System).EnableLua -ne 0
}

#
# Main script
#
if (!(IsAdministrator))
{
    if (IsUacEnabled)
    {
        [string[]]$argList = @('-NoProfile', '-File', $MyInvocation.MyCommand.Path)
        $argList += $MyInvocation.BoundParameters.GetEnumerator() | Foreach {"-$($_.Key)", "$($_.Value)"}
        $argList += $MyInvocation.UnboundArguments
        Start-Process PowerShell.exe -Verb Runas -WorkingDirectory $pwd -ArgumentList $argList 
        return
    }
    else
    {
        throw "You must be administrator to run this script"
    }
}

# Copy in all the driver files
Copy-Item -Path "\\vmware-host\Shared Folders\gbhv\*" -Force -Destination "C:\Users\Gbps\Desktop\gbhv" -Recurse

# Create and start the driver service
sc.exe stop gbhv 2>&1 | Out-Null
sc.exe delete gbhv 2>&1 | Out-Null
sc.exe create gbhv binPath= C:\Users\Gbps\Desktop\gbhv\gbhv.sys type= kernel
sc.exe start gbhv
