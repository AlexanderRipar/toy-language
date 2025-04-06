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

if ($IsLinux)
{
	Write-Warning "Currently running on linux only supports running g++ and clang.`nIf you are running from wsl and want to include msvc, exit wsl and rerun this script."

	$specs = @(
		@{
			jobname = 'build-clang-linux'
			command = 'cmake -S . -B build_clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang' + 
			      ' && cmake --build build_clang --target ' + $targets + ' 2>&1'
		},
		@{
			jobname = 'build-gcc-linux'
			command = 'cmake -S . -B build_gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc' +
			      ' && cmake --build build_gcc --target ' + $targets + ' 2>&1'
		}
	)
}
elseif ($IsWindows)
{
	$specs = @(
		@{
			jobname = 'build-clang-linux'
			command = 'wsl -- cmake -S . -B build_clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_C_COMPILER=clang' + 
			           ' `&`& cmake --build build_clang --target ' + $targets + ' 2`>`&1'
		},
		@{
			jobname = 'build-gcc-linux'
			command = 'wsl -- cmake -S . -B build_gcc -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc' +
			           ' `&`& cmake --build build_gcc --target ' + $targets + ' 2`>`&1'
		},
		@{
			jobname = 'build-msvc-windows'
			command = 'cmake -S . -B build_msvc -DCMAKE_CXX_COMPILER=msvc -DCMAKE_C_COMPILER=msvc' +
			      ' && cmake --build build_msvc --target ' + ($targets -creplace ' all ', ' ALL_BUILD ') + ' 2>&1'
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
		-ScriptBlock { $input | Foreach-Object { Invoke-Expression $_.command } } `
		-Name $_.jobname `
		-InputObject $_
}

# Receive job results separately for each job, as we otherwise get an
# intermingling of their outputs.
# Separately wait for any jobs to complete and print their results immediately
# for improved responsiveness.

while ($jobs.Count -ne 0)
{
	$completed_job = Wait-Job -Job $jobs -Any

	$jobs = $jobs | Where-Object { $_ -ne $completed_job }

	@('', '', "################# $($completed_job.Name) #################", '') + (Receive-Job $completed_job)

	Remove-Job $completed_job
}
