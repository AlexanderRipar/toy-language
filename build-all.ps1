param(
	[parameter(ValueFromRemainingArguments = $true)]
	[string[]] $targets
)

if ($targets.Count -eq 0)
{
	"Usage: $PSCommandPath [targets...]"

	exit 1
}

$targets = ' ' + ($targets -join ' ') + ' '

$warnings = @()

if ($IsLinux)
{
	$no_msvc_warning = "Currently running on linux only supports running g++ and clang.`nIf you are running from wsl and want to include msvc, exit wsl and rerun this script."

	Write-Warning $no_msvc_warning

	$warnings += $no_msvc_warning

	$specs = @(
		@{
			jobname = 'build-clang-linux'
			command = 'cmake -S . -B build/clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang' +
			      ' && cmake --build build/clang --target ' + $targets + ' 2>&1'
		},
		@{
			jobname = 'build-gcc-linux'
			command = 'cmake -S . -B build/gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc' +
			      ' && cmake --build build/gcc --target ' + $targets + ' 2>&1'
		}
	)
}
elseif ($IsWindows)
{
	$specs = @(
		@{
			jobname = 'build-clang-linux'
			command = 'wsl -- cmake -S . -B build/clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang' +
			           ' `&`& cmake --build build/clang --target ' + $targets + ' 2`>`&1'
		},
		@{
			jobname = 'build-gcc-linux'
			command = 'wsl -- cmake -S . -B build/gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc' +
			           ' `&`& cmake --build build/gcc --target ' + $targets + ' 2`>`&1'
		},
		@{
			jobname = 'build-msvc-windows'
			command = 'cmake -S . -B build/msvc -DCMAKE_CXX_COMPILER=msvc -DCMAKE_C_COMPILER=msvc' +
			      ' && cmake --build build/msvc --target ' + ($targets -creplace ' all ', ' ALL_BUILD ') + ' 2>&1'
		}
	)
}
else
{
	Write-Error "Your OS is currently not supported."

	exit 1
}


# Start one asynchronous job for each build.
$jobs = $specs | Foreach-Object {
	Start-Job `
		-ScriptBlock { $input | Foreach-Object { @{ output = Invoke-Expression $_.command; exitcode = $LASTEXITCODE } } } `
		-Name $_.jobname `
		-InputObject $_
}

# Receive job results separately for each job, as we otherwise get an
# intermingling of their outputs.
# Separately wait for any jobs to complete and print their results immediately
# for improved responsiveness.

$failed_jobs = @()

while ($jobs.Count -ne 0)
{
	$completed_job = Wait-Job -Job $jobs -Any

	$jobs = $jobs | Where-Object { $_ -ne $completed_job }

	$job_result = Receive-Job $completed_job

	@('', '', "################# $($completed_job.Name) #################", '') + $job_result.output

	if ($job_result.exitcode -ne 0)
	{
		$failed_jobs += "$($completed_job.Name) ($($job_result.exitcode))"
	}

	Remove-Job $completed_job
}

if ($warnings.Count -ne 0)
{
	Write-Host "`nThe following warnings were generated:`n    - $($warnings -join "`n    - ")" -ForegroundColor Yellow
}

if ($failed_jobs.Count -ne 0)
{
	Write-Host "`nThe following builds failed:`n    - $($failed_jobs -join "`n    - ")" -ForegroundColor Red
}
else
{
	Write-Host "`nAll builds succeeded" -ForegroundColor Green
}
