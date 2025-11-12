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
			jobname = 'linux-clang-deb'
			command = 'wsl -- cmake -S . -B build/clang-deb -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=debug' +
			           ' `&`& cmake --build build/clang-deb --target ' + $targets + ' 2`>`&1'
		},
		@{
			jobname = 'linux-clang-rel'
			command = 'wsl -- cmake -S . -B build/clang-rel -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=release' +
			           ' `&`& cmake --build build/clang-rel --target ' + $targets + ' 2`>`&1'
		},
		@{
			jobname = 'linux-gcc-deb'
			command = 'wsl -- cmake -S . -B build/gcc-deb -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc  -DCMAKE_BUILD_TYPE=debug' +
			           ' `&`& cmake --build build/gcc-deb --target ' + $targets + ' 2`>`&1'
		},
		@{
			jobname = 'linux-gcc-rel'
			command = 'wsl -- cmake -S . -B build/gcc-rel -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc  -DCMAKE_BUILD_TYPE=release' +
			           ' `&`& cmake --build build/gcc-rel --target ' + $targets + ' 2`>`&1'
		}
	)
}
elseif ($IsWindows)
{
	$specs = @(
		@{
			jobname = 'linux-clang-deb'
			command = 'wsl -- cmake -S . -B build/clang-deb -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=debug' +
			           ' `&`& cmake --build build/clang-deb --target ' + $targets + ' 2`>`&1'
		},
		@{
			jobname = 'linux-clang-rel'
			command = 'wsl -- cmake -S . -B build/clang-rel -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -DCMAKE_BUILD_TYPE=release' +
			           ' `&`& cmake --build build/clang-rel --target ' + $targets + ' 2`>`&1'
		},
		@{
			jobname = 'linux-gcc-deb'
			command = 'wsl -- cmake -S . -B build/gcc-deb -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc  -DCMAKE_BUILD_TYPE=debug' +
			           ' `&`& cmake --build build/gcc-deb --target ' + $targets + ' 2`>`&1'
		},
		@{
			jobname = 'linux-gcc-rel'
			command = 'wsl -- cmake -S . -B build/gcc-rel -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc  -DCMAKE_BUILD_TYPE=release' +
			           ' `&`& cmake --build build/gcc-rel --target ' + $targets + ' 2`>`&1'
		},
		@{
			jobname = 'win32-msvc-deb'
			command = 'cmake -S . -B build/msvc-deb -DCMAKE_CXX_COMPILER=msvc -DCMAKE_C_COMPILER=msvc' +
			      ' && cmake --build build/msvc-deb --config debug --target ' + ($targets -creplace ' all ', ' ALL_BUILD ') + ' 2>&1'
		},
		@{
			jobname = 'win32-msvc-rel'
			command = 'cmake -S . -B build/msvc-rel -DCMAKE_CXX_COMPILER=msvc -DCMAKE_C_COMPILER=msvc' +
			      ' && cmake --build build/msvc-rel --config release --target ' + ($targets -creplace ' all ', ' ALL_BUILD ') + ' 2>&1'
		},
		@{
			jobname = 'win32-clang-cl-deb'
			command = 'cmake -S . -B build/clang-cl-deb -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang -Tclangcl' +
			      ' && cmake --build build/clang-cl-deb --config debug --target ' + ($targets -creplace ' all ', ' ALL_BUILD ') + ' 2>&1'
		},
		@{
			jobname = 'win32-clang-cl-rel'
			command = 'cmake -S . -B build/clang-cl-rel -DCMAKE_CXX_COMPILER=cclang++ -DCMAKE_C_COMPILER=clang -Tclangcl' +
			      ' && cmake --build build/clang-cl-rel --config release --target ' + ($targets -creplace ' all ', ' ALL_BUILD ') + ' 2>&1'
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
		-ScriptBlock { $input | Foreach-Object {
			# While this does not preserve the ANSI escape sequences in gcc's
			# output, it at least makes them disappear instead of creating
			# mojibake.
			[Console]::OutputEncoding = [Text.UTF8Encoding]::UTF8;
			@{ output = Invoke-Expression $_.command; exitcode = $LASTEXITCODE }
		} } `
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

	Write-Output ''
	Write-Output ''
	Write-Output "################# $($completed_job.Name) #################"
	Write-Output ''
	Write-Output $job_result.output

	# Write-Output @('', '', "################# $($completed_job.Name) #################", '') + $job_result.output

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
