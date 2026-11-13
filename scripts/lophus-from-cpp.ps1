param(
	$ast
)

[hashtable]$custom_operators = @{
	'operator+'   = 'cpp_operator_add'
	'operator-'   = 'cpp_operator_sub'
	'operator*'   = 'cpp_operator_mul'
	'operator/'   = 'cpp_operator_div'
	'operator%'   = 'cpp_operator_mod'
	'operator&'   = 'cpp_operator_bitand'
	'operator|'   = 'cpp_operator_bitor'
	'operator^'   = 'cpp_operator_bitxor'
	'operator+='  = 'cpp_operator_add_set'
	'operator-='  = 'cpp_operator_sub_set'
	'operator*='  = 'cpp_operator_mul_set'
	'operator/='  = 'cpp_operator_div_set'
	'operator%='  = 'cpp_operator_mod_set'
	'operator&='  = 'cpp_operator_bitand_set'
	'operator|='  = 'cpp_operator_bitor_set'
	'operator^='  = 'cpp_operator_bitxor_set'
	'operator++'  = 'cpp_operator_inc'
	'operator--'  = 'cpp_operator_dec'
	'operator='   = 'cpp_operator_set'
	'let'         = 'cpp_let'
	'pub'         = 'cpp_pub'
	'mut'         = 'cpp_mut'
	'global'      = 'cpp_global'
	'where'       = 'cpp_where'
	'try'         = 'cpp_try'
	'catch'       = 'cpp_catch'
	'finally'     = 'cpp_finally'
	'undefined'   = 'cpp_undefind'
	'unreachable' = 'cpp_unreachable'
	'func'        = 'cpp_func'
	'proc'        = 'cpp_proc'
	'ensures'     = 'cpp_ensures'
	'requires'    = 'cpp_requires'
	'trait'       = 'cpp_trait'
	'impl'        = 'cpp_impl'
}

[string[]]$skipped_record_elems = @('CXXConstructorDecl', 'CXXDestructorDecl', 'CXXMethodDecl', 'AlignedAttr', 'TypeVisibilityAttr', 'PackedAttr', 'MayAliasAttr', 'StaticAssertDecl', 'IndirectFieldDecl')

[string[]]$skipped_field_value_kinds = @('AlignedAttr', 'MayAliasAttr', 'PackedAttr', 'TypeVisibilityAttr')

function strip($node) {
	$properties = $node.PSObject.Properties

	$properties.Remove('loc');
	$properties.Remove('range');
	$properties.Remove('id');
	$properties.Remove('language');
	$properties.Remove('mangledName');

	$properties | foreach {
		if ($_.TypeNameOfValue -eq 'System.Object[]') {
			$_.Value | foreach { strip $_ }
		}
	}
}

function map_name([string]$name) {
	$operator_name = $custom_operators[$name]

	if ($operator_name) {
		return "$operator_name"
	} elseif ($name.StartsWith('_')) {
		return "cpp_leading_underscore$name"
	} else {
		return "$name"
	}
}

function map_type_rec([string[]]$tokens, [int]$curr) {
	[string]$mapped = ''
	[bool]$is_const = $false
	[string]$is_anon = ''

	while ($curr -ne $tokens.Count) {
		[string]$tok = $tokens[$curr]

		$curr += 1

		if ($tok -eq 'volatile' -or $tok -eq 'struct' -or $tok -eq 'union' -or $tok -eq 'enum' -or $tok -eq 'class') {
			# Ignore volatile, struct, union, enum and class in type names.
		} elseif ($tok -eq '__attribute__') {
			# Ignore __attribute__s.
			[int]$paren_nesting = 0
			while ($true) {
				if ($curr -eq $tokens.Count) {
					Write-Error "Unmatched parentheses in '__attribute__'."
					exit
				} elseif ($tokens[$curr] -eq '(') {
					$paren_nesting += 1
				} elseif ($tokens[$curr] -eq ')') {
					$paren_nesting -= 1
					if ($paren_nesting -eq 0) {
						$curr += 1
						break
					}
				}
				$curr += 1
			}
		} elseif ($tok -eq 'const') {
			$is_const = $true
		} elseif ($tok -eq '*' -or $tok -eq '&' -or $tok -eq '&&') {
			$mapped = if ($is_const) {
				$is_const = $false
				'*' + $mapped
			} else {
				'*mut ' + $mapped
			}
		} elseif ($tok -eq '[') {
			[string[]]$count_expr = ''
			while ($tokens[$curr] -ne ']') {
				$count_expr += $tokens[$curr]
				$curr += 1
			}

			$curr += 1

			$mapped = if ($count_expr) {
				"[$(($count_expr -join ' ').Trim())]" + $mapped
			} elseif ($is_const) {
				$is_const = $false
				'*' + $mapped
			} else {
				'*mut ' + $mapped
			}
		} elseif ($tok -eq '<') {
			$mapped += '('

			while ($tokens[$curr] -ne '>') {
				$rec_result = map_type_rec $tokens $curr

				$curr = $rec_result.curr
				$mapped += $rec_result.mapped

				if ($tokens[$curr] -eq ',') {
					$mapped += ', '
					$curr += 1
				}
			}

			$mapped += ')'

			$curr += 1
		} elseif ($tok -eq '::') {
			$mapped += '.'
		} elseif ($tok -match '^[_a-zA-Z][_a-zA-Z0-9]*$') {
			$mapped += map_name $tok
		} elseif ($tok -eq '>' -or $tok -eq ',') {
			$curr -= 1
			break
		} elseif ($tok -eq '(') {
			[string]$anon = $tokens[$curr]

			if ($anon -ne 'anonymous' -and $anon -ne 'unnamed') {
				Write-Error "Expected parentheses in type to contain an unnamed type at index $curr in $($tokens | foreach { "'$_'" })"
				exit
			}

			$curr += 1

			[string]$kind = $tokens[$curr]

			if ($kind -notmatch '^[_a-zA-Z][_a-zA-Z0-9]*$') {
				Write-Error "Expected parentheses in type to contain an unnamed type at index $curr in $($tokens | foreach { "'$_'" })"
				exit
			}

			$curr += 1

			[int]$nesting = 1

			while ($nesting -ne 0) {
				if ($curr -eq $tokens.Count) {
					Write-Error "Unmatched parenthesis in type $($tokens | foreach { "'$_'" })"
				} elseif ($tokens[$curr] -eq '(') {
					$nesting += 1
				} elseif ($tokens[$curr] -eq ')') {
					$nesting -= 1
				}

				$curr += 1
			}

			$is_anon = $kind
		} else {
			Write-Error "Unexpected type token '$tok' at index $curr in $($tokens | foreach { "'$_'" })"
			exit
		}
	}

	return @{
		curr = $curr
		mapped = $mapped
		is_const = $is_const
		anon = $is_anon
	}
}

function map_type([string]$type) {
	[string[]]$tokens = $type.Replace(',', ' , ').Replace('<', ' < ').Replace('(', ' ( ').Replace(')', ' ) ').Replace('>', ' > ').Replace('*', ' * ').Replace('[', ' [ ').Replace(']', ' ] ').Replace('::', ' :: ').Split(' ') | where { $_ -ne '' -and $_ -ne ' ' }

	if ($tokens.Count -eq 0) {
		Write-Error "Empty type literal"
		exit
	}

	$result = map_type_rec $tokens 0

	if ($result.curr -ne $tokens.Count) {
		Write-Error "Type literal ``$type`` was not fully parsed"
		exit
	}

	if ($result.anon -ne '') {
		$result.mapped = ''
	}

	return $result
}

function map_parameter([string]$name, [string]$type, [string]$default) {
	$name = if (-not $name) {
		'cpp_unnamed_parameter_' + (New-Guid).ToString().Replace('-', '_')
	} else {
		map_name $name
	}

	$type_result = map_type $type

	[string]$introducer = if ($type_result.is_const) { 'let' } else { 'mut' }

	return "$introducer ${name}: $($type_result.mapped)"
}

function indent {
	param(
		[int]$nesting,
		[ValidateSet('NewLine', 'SameLine', 'NoSpace', 'BlockSameLine', 'BlockOrIfSameLine')]
		[string]$line
	)

	if ($line -eq 'SameLine') {
		return ' '
	} elseif ($line -eq 'NoSpace') {
		return ''
	} else {
		return "`n" + ("`t" * $nesting)
	}
}

function map_expr {
	param(
		$node,
		[int]$nesting,
		[ValidateSet('NewLine', 'SameLine', 'NoSpace', 'BlockSameLine', 'BlockOrIfSameLine')]
		[string]$line
	)

	switch -Regex ($node.kind) {
	'^CompoundStmt$' {
		if ($line -ne 'NewLine') {
			$nesting -= 1
		}

		if ($line -eq 'BlockSameLine' -or $line -eq 'BlockOrIfSameLine') {
			$line = 'SameLine'
		}

		[string]$end_line = if ($node.inner.Count -eq 0) { 'SameLine' } else { 'NewLine' }

		return "$(indent $nesting $line){$($node.inner | foreach { "$(map_expr $_ ($nesting + 1) NewLine)" })$(indent $nesting $end_line)}"
	}

	'^CharacterLiteral$' {
		[int]$codepoint = $node.value

		[string]$value = if ($codepoint -eq [int][char]"`n") {
			'\n'
		} elseif ($codepoint -eq [int][char]"`r") {
			'\r'
		} elseif ($codepoint -eq [int][char]"`t") {
			'\t'
		} elseif ($codepoint -eq [int][char]'\') {
			'\\'
		} elseif ($codepoint -eq [int][char]"'") {
			"\'"
		} elseif ($codepoint -ge 32 -and $codepoint -le 127) {
			[char]$codepoint
		} elseif ($codepoint -le 255) {
			"\x$($codepoint.ToString('X').PadLeft(2, '0'))"
		} else {
			"\X$($codepoint.ToString('X').PadLeft(6, '0'))"
		}

		return "$(indent $nesting $line)'$value'"
	}

	'^PredefinedExpr$' {
		if ($node.name -eq '__FUNCTION__') {
			$string = $node.inner[0]

			if ($string.kind -ne 'StringLiteral') {
				Write-Error "Expected first child of 'PredefinedExpr' to be 'StringLiteral' but was '$($string.name)'"
				exit
			}

			return "$(indent $nesting $line)$($string.value)"
		} else {
			Write-Error "Unknown 'PredefinedExpr' '$($node.name)'"
			exit
		}
	}

	'^CXXForRangeStmt$' {
		# [0]: Empty
		# [1]: __range1 = <range>
		# [2]: __begin1 = __range1.begin
		# [3]: __end1 = __range1.end
		# [4]: __begin1 != __end1
		# [5]: __begin1++
		# [6]: <elem> = *__begin1
		# [7]: <body>

		$range = $node.inner[1].inner[0].inner

		$elem = map_name $node.inner[6].inner.name

		$body = $node.inner[7]

		return "for $elem <- $(map_expr $range ($nesting + 1) NoSpace) do$(map_expr $body ($nesting + 1) BlockSameLine)"
	}

	'^ParenExpr$' {
		return "($(map_expr $node.inner[0] $nesting NoSpace))"
	}

	'^ContinueStmt$' {
		return "$(indent $nesting $line)continue"
	}

	'^BreakStmt$' {
		return "$(indent $nesting $line)break {}"
	}

	'^ConditionalOperator$' {
		$condition = $node.inner[0]
		$consequent = $node.inner[1]
		$alternative = $node.inner[2]

		return "$(indent $nesting $line)if $(map_expr $condition ($nesting + 1) NoSpace) then$(map_expr $consequent ($nesting + 1) BlockSameLine)`nelse$(map_expr $alternative ($nesting + 1) BlockSameLine)"
	}

	'^CompoundAssignOperator$' {
		$lhs = $node.inner[0]
		$rhs = $node.inner[1]

		return "$(indent $nesting $line)$(map_expr $lhs ($nesting + 1) NoSpace) = $(map_expr $rhs ($nesting + 1) NoSpace)"
	}

	'^TypedefDecl$' {
		[string]$introducer = if ($nesting -eq 0) { 'pub' } else { 'let' }
		[string]$name = map_name $node.name
		[string]$value = (map_type $node.type.qualType).mapped


		return "$(indent $nesting $line)$introducer $name = $value"
	}

	'^ArraySubscriptExpr$' {
		$lhs = $node.inner[0]
		$rhs = $node.inner[1]

		return "$(indent $nesting $line)$(map_expr $lhs ($nesting + 1) NoSpace)[$(map_expr $rhs ($nesting + 1) NoSpace)]"
	}

	'^NullStmt$' {
		return "$(indent $nesting $line){}"
	}

	'^ReturnStmt$' {
		if ($node.inner -and $node.inner.Count -ne 0) {
			return "$(indent $nesting $line)return $(map_expr $node.inner[0] ($nesting + 1) NoSpace)"
		} else {
			return "$(indent $nesting $line)return {}"
		}
	}

	'^(CStyleCastExpr|ImplicitCastExpr|CXXStaticCastExpr|CXXReinterpretCastExpr|CXXFunctionalCastExpr|BuiltinBitCastExpr)$' {
		[string]$castKind = $node.castKind
		$operand = $node.inner[0]

		if ($castKind -eq 'NoOp' -or $castKind -eq 'LValueToRValue' -or $castKind -eq 'FunctionToPointerDecay' -or $castKind -eq 'BuiltinFnToFnPtr') {
			return map_expr $operand $nesting $line
		} else {
			$cast_op = if ($node.kind -in 'BuiltinBitCastExpr', 'CXXReinterpretCastExpr') { 'bitcast' } else { 'cast' }

			return "$(indent $nesting $line)$cast_op($((map_type $node.type.qualType).mapped), $(map_expr $operand ($nesting + 1) NoSpace))"
		}
	}

	'^DeclStmt$' {
		if ((-not $node.inner) -or $node.inner.Count -eq 0) {
			Write-Error "Expected 'DeclStmt' to contain exactly one child"
			exit
		}
		
		$decl = $node.inner[0]

		if ($decl.kind -ne 'VarDecl' -and $decl.kind -ne 'TypedefDecl' -and $decl.kind -ne 'CXXRecordDecl') {
			Write-Error "Expected 'DeclStmt' to contain exactly a single 'VarDecl', 'TypedefDecl' or 'CXXRecordDecl' but got $($node.inner[0].kind)"
			exit
		}

		return map_expr $decl $nesting $line
	}

	'^VarDecl$' {
		[string]$name = map_name $node.name
		$type = map_type $node.type.qualType

		[string]$introducer = if ($type.is_const) {
			if ($nesting -eq 0) { 'pub' } else { 'let' }
		} else {
			if ($nesting -eq 0) { 'pub mut' } else { 'mut' }
		}

		if ($node.inner -and $node.inner.Count -ne 0) {
			if ($node.inner.Count -ne 1) {
				Write-Error "Expected 'VarDecl' to contain either zero or one initializer value but found $($node.inner.Count)"
				exit
			}

			return "$(indent $nesting $line)$introducer $name = $(map_expr $node.inner[0] ($nesting + 1) NoSpace)"
		} else {
			return "$(indent $nesting $line)$introdcuer $name = undefined"
		}
	}

	'^IfStmt$' {
		[int]$offset = 0

		if ($line -ne 'NewLine') {
			$nesting -= 1
		}

		if ($line -eq 'BlockOrIfSameLine') {
			$line = 'SameLine'
		}

		$where = if ($node.hasInit) {
			$offset = 1
			" where $(map_expr $node.inner[0] ($nesting + 1) NoSpace)"
		} else {
			''
		}

		$condition = map_expr $node.inner[$offset] ($nesting + 1) NoSpace

		$consequent = map_expr $node.inner[$offset + 1] ($nesting + 1) BlockSameLine

		$offset += 2

		$else = if ($node.hasElse) {
			$offset += 1
			[string]$else_line = if ($consequent.StartsWith(' {') -and $consequent.EndsWith('}')) { 'SameLine' } else { 'NewLine' }

			"$(indent $nesting $else_line)else$(map_expr $node.inner[$offset - 1] ($nesting + 1) BlockOrIfSameLine)"
		} else {
			''
		}

		if ($node.inner.Count -ne $offset) {
			Write-Error "Expected 'IfStmt' to have $offset children but found $($node.inner.Count)"
			exit
		}

		return "$(indent $nesting $line)if $condition$where then$consequent$else"
	}

	'^BinaryOperator$' {
		if ($node.inner.Count -ne 2) {
			Write-Error "Expected 'BinaryOperator' to have exactly 2 children but found $($node.inner.Count)."
			exit
		}

		$opcode = $node.opcode

		$lhs = $node.inner[0]
		$rhs = $node.inner[1]

		[string]$prefix = indent $nesting $line

		[string]$lhs = map_expr $node.inner[0] ($nesting + 1) NoSpace
		[string]$rhs = map_expr $node.inner[1] ($nesting + 1) NoSpace

		if ($opcode -eq '.') {
			return "$prefix($lhs.$rhs)"
		} elseif ($opcode -eq '.*') {
			return "$prefix($lhs.$rhs.&)"
		} elseif ($opcode -eq '->') {
			return "$prefix($lhs.*.$rhs)"
		} elseif ($opcode -eq '->*') {
			return "$prefix($lhs.*.$rhs.&)"
		} elseif ($opcode -eq '=') {
			return "$prefix$lhs $opcode $rhs"
		} else {
			return "$prefix($lhs $opcode $rhs)"
		}
	}

	'^UnaryOperator$' {
		if ($node.inner.Count -ne 1) {
			Write-Error "Expected 'UnaryOperator' to have exactly 1 child but found $($node.inner.Count)."
			exit
		}

		[string]$opcode = $node.opcode
		[string]$operand = map_expr $node.inner[0] ($indent + 1) NoSpace
		[string]$prefix = indent $indent $line
		if ($opcode -eq '&') {
			return "$prefix($operand).&"
		} elseif ($opcode -eq '*') {
			return "$prefix($operand).*"
		} elseif ($opcode -eq '++') {
			if ($node.isPostfix) {
				return "${prefix}postinc($operand.&)"
			} else {
				return "${prefix}preinc($operand.&)"
			}
		} elseif ($opcode -eq '--') {
			if ($node.isPostfix) {
				return "${prefix}postdec($operand.&)"
			} else {
				return "${prefix}predec($operand.&)"
			}
		} elseif ($opcode -eq '__extension__') {
			return "$prefix$operand"
		} elseif ($node.isPostfix) {
			return "$prefix($operand)$opcode"
		} else {
			return "$prefix$opcode($operand)"
		}
	}

	'^UnaryExprOrTypeTraitExpr$' {
		[string]$name = $node.name
		[string]$prefix = indent $nesting $line

		if ($node.argType) {
			return "$prefix$name($($node.argType.qualType))"
		} elseif ($node.inner -and $node.inner.Count -eq 1) {
			return "$prefix$name($(map_expr $node.inner[0] ($nesting + 1) NoSpace))"
		} else {
			Write-Error "Expected 'UnaryExprOrTypeTraitExpr' to have exactly one of 'argType' or a single child."
			exit
		}
	}

	'^CXXNullPtrLiteralExpr$' {
		return "$(indent $nesting $line)null"
	}

	'^SwitchStmt$' {
		$switched = $node.inner[0]

		[string]$output = "$(indent $nesting $line)switch $(map_expr $switched ($nesting + 1) NoSpace)"

		$cases = $node.inner
		[int]$cases_start = 1
		
		if ($cases[1].kind -eq 'CompoundStmt') {
			Write-Host "Compound Switch"
			$cases = $cases[1].inner
			$cases_start = 0
		}

		$cases | Select-Object -Skip $cases_start | foreach {
			if ($_.kind -eq 'CaseStmt') {
				if (-not $_.inner) {
					Write-Error "Expected 'CaseStmt' to have at least 1 child."
					exit
				}

				[bool]$is_single_stmt = $curr.inner.Count -eq 2

				[int]$extra_nesting = if ($is_single_stmt) { 1 } else { 2 }

				$output += "$(indent ($nesting + 1) NewLine)case $(map_expr $_.inner[0] ($nesting + 1) NoSpace) -> $(if ($is_single_stmt) { '' } else { '{' })"

				[string]$stmt_line = if ($is_single_stmt) { 'BlockSameLine' } else { 'NewLine' }

				$_.inner | Select-Object -Skip 1 | foreach {
					$output += " $(map_expr $_ ($nesting + $extra_nesting) $stmt_line)"
					$stmt_line = 'NewLine'
				}

				if (-not $is_single_stmt) {
					$output += "$(indent ($nesting + 1) NewLine)}"
				}
			} elseif ($curr.kind -eq 'DefaultStmt') {
				[bool]$is_single_stmt = $curr.inner.Count -eq 1

				[int]$extra_nesting = if ($is_single_stmt) { 1 } else { 2 }

				$output += "$(indent ($nesting + 1) NewLine)case _ -> $(if ($is_single_stmt) { '' } else { '{' })"

				[string]$stmt_line = if ($is_single_stmt) { 'BlockSameLine' } else { 'NewLine' }

				$curr.inner | foreach {
					$output += " $(map_expr $_ ($nesting + $extra_nesting) $stmt_line)"
					$stmt_line = 'NewLine'
				}

				if (-not $is_single_stmt) {
					$output += "$(indent ($nesting + 1) NewLine)}"
				}
			} elseif ($curr.kind) {
				# TODO: Allow multiple cases for the same block by special-casing this.
				Write-Warning "Expected 'SwitchStmt' to only contain 'CaseStmt' and 'DefaultStmt' but found '$($curr.kind)'."
			}
		}

		return $output
	}

	'^CompoundLiteralExpr$' {
		if (-not $node.inner -or $node.inner.Count -ne 1 -or $node.inner[0].kind -ne 'InitListExpr') {
			Write-Error "Expected 'CompoundLiteralExpr' to contain exactly one 'InitListExpr' but found '$($node.inner)'"
			exit
		}

		return map_expr $node.inner[0] $nesting $line
	}

	'^StringLiteral$' {
		return "$(indent $nesting $line)$($node.value)"
	}

	'^(CXXDefaultInitExpr|ExprWithCleanups|ConstantExpr|MaterializeTemporaryExpr|ElaboratedType)$' {
		if ($node.inner.Count -ne 1) {
			Write-Error "Expected '$($node.kind)' to contain exactly one child $($node.inner.Count)"
			exit
		}

		return map_expr $node.inner[0] $nesting $line
	}

	'^CXXMemberCallExpr$' {
		$call = $node.inner[0]

		if ($call.kind -ne 'MemberExpr') {
			Write-Error "Expected first child of 'CXXMemberCallExpr' to be a 'MemberExpr' but got a $($call.kind)."
			exit
		}

		[string]$output = "$(indent $nesting $line)$(map_name $call.name)("

		[bool]$is_first = $true

		$call.inner | Select-Object -Skip 1 | foreach {
			if (-not $is_first) {
				$output += ', '
			}

			$output += map_expr $_ ($nesting + 1) NoSpace
		}

		return "$output)"
	}

	'^DoStmt$' {
		if ($node.inner.Count -ne 2) {
			Write-Error "Expected 'DoStmt' to have exactly 2 children but got $($node.inner.Count)."
			exit
		}

		return "$(indent $nesting $line)for true do {$(indent ($nesting + 1) NewLine){$(map_expr $node.inner[0] ($nesting + 2) NewLine)$(indent ($nesting + 1) NewLine)}$(indent ($nesting + 1) NewLine)if !($(map_expr $node.inner[1] ($nesting + 1) NoSpace)) then $(indent ($nesting + 2) NewLine)break {}$(indent $nesting NewLine)}"
	}

	'^(CXXOperatorCallExpr|CallExpr)$' {
		if (-not $node.inner -or $node.inner.Count -lt 1) {
			Write-Error "Expected '$($node.kind)' to have at least one child."
			exit
		}

		$callee = $node.inner[0]

		[string]$output = "$(map_expr $callee $nesting $line)("

		[bool]$is_first = $true

		$node.inner | Select-Object -Skip 1 | foreach {
			if (-not $is_first) {
				$output += ', '
			} else {
				$is_first = $false
			}

			$output += map_expr $_ ($nesting + 1) NoSpace
		}

		return "$output)"
	}

	'^WhileStmt$' {
		if ($node.inner.Count -ne 2) {
			Write-Error "Expected '$($node.kind) to have exactly two children but found $($node.inner.Count)."
			exit
		}

		return "$(indent $nesting $line)for $(map_expr $node.inner[0] ($nesting + 1) NoSpace) do$(map_expr $node.inner[1] ($nesting + 1) BlockSameLine)"
	}

	'^InitListExpr$' {
		[string]$output = "$(indent $nesting $line).{"

		[bool]$is_first = $true

		$node.inner | foreach {
			# TODO: Figure out why there are nulls in here.
			if ($_.kind) {
				if (-not $is_first) {
					$output += ', '
				} else {
					$is_first = $false
				}

				$output += map_expr $_ ($nesting + 1) NewLine
			}
		}

		[string]$end_line = if ($node.inner.Count) { 'NewLine' } else { 'SameLine' }

		return "$output$(indent $nesting $end_line)}"
	}

	'^BuiltinType$' {
		return "$(indent $nesting $line)$(map_type $node.type.qualType)"
	}

	'^GCCAsmStmt$' {
		[string]$output = "$(indent $nesting $line)/* cpp inline asm */ asm_statement("

		[bool]$is_first = $true

		$node.inner | foreach {
			if (-not $is_first) {
				$output += ', '
			} else {
				$is_first = $false
			}

			$output += map_expr $_ ($nesting + 1) NoSpace
		}

		return "$output)"
	}

	'^ForStmt$' {
		if ($node.inner.Count -ne 5) {
			Write-Error "Expected 'ForStmt' to have exactly 5 children but found $($node.inner.Count)."
			exit
		}

		if ($node.inner[1].kind) {
			Write-Error "Expected second child of 'ForStmt' to be empty but found '$($node.inner[1].kind)."
			exit
		}

		[string] $where = if ($node.inner[0].kind) {
			" where $(map_expr $node.inner[0] ($nesting + 1) NoSpace)"
		} else {
			''
		}

		[string]$guard = if ($node.inner[2].kind) {
			map_expr $node.inner[2] ($nesting + 1) SameLine
		} else {
			' true'
		}

		[string]$step = if ($node.inner[3].kind) {
			", $(map_expr $node.inner[3] ($nesting + 1) NoSpace)"
		}

		return "$(indent $nesting $line)for${guard}${step} do$(map_expr $node.inner[4] ($nesting + 1) BlockSameLine)"
	}

	'^MemberExpr$' {
		if ($node.inner.Count -ne 1) {
			Write-Error "Expected 'MemberExpr' to have exactly one child but found $($node.inner.Count)."
			exit
		}

		if (-not $node.name) {
			# This happens for anonymous union or struct members
			return map_expr $node.inner[0] $nesting $line
		} else {
			[string]$addr = if ($node.isArrow) { '.&' } else { '' }

			return "$(map_expr $node.inner[0] $indent $line).$(map_name $node.name)$addr"
		}
	}

	'^FieldDecl$' {
		[string]$name = map_name $node.name

		if ($name -eq '') {
			$name = 'cpp_unnamed_field_' + (New-Guid).ToString().Replace('-', '_')
		}

		$type = map_type $node.type.qualType

		[string]$introducer = if ($type.is_const) { 'let' } else { 'mut' }

		[string]$default = if ($node.inner -and $node.inner[0].kind -notin $skipped_field_value_kinds) {
			if ($node.inner.Count -ne 1) {
				Write-Error "Expected 'FieldDecl to have either 0 or 1 children but found $($node.inner.Count)."
				exit
			}

			" = $(map_expr $node.inner[0] ($nesting + 1) NoSpace)"
		} else {
			''
		}

		return "$(indent $nesting $line)$introducer ${name}: $($type.mapped)$default"
	}

	'^(CXXBoolLiteralExpr|FloatingLiteral)$' {
		return "$(indent $nesting $line)$($node.value)"
	}

	'^IntegerLiteral$' {
		[uint64]$value = $node.value

		# Set a limit on integer constants, since particularly large ones (> (2^63)-1) aren't supported yet
		if ($value -gt 10000000) {
			$value = 10000000
		}

		return "$(indent $nesting $line)$value"
	}

	'^CXXRecordDecl$' {
		if (-not $node.completeDefinition) {
			return ''
		}

		[string]$name = map_name $node.name

		[string]$introducer = if ($nesting -eq 0) { 'pub' } else { 'let' }

		[string]$tag = if ($node.tagUsed -eq 'struct' -or $node.tagUsed -eq 'class') {
			'CStruct'
		} elseif ($node.tagUsed -eq 'union') {
			'CUnion'
		} else {
			Write-Error "Expected 'CXXRecordDecl' to have a 'tagUsed' value of 'struct', 'class' or 'union' but found '$($node.tagUsed)'."
			exit
		}

		[string]$output = "$(indent $nesting $line)$introducer $name = $tag("

		if (-not $node.inner -or $node.inner.Count -eq 0) {
			return "$output)"
		}

		[int]$i = 0

		[string]$prev_anonymous_record = ''

		while ($i -ne $node.inner.Count) {
			$curr = $node.inner[$i]
			$i += 1
			[string]$kind = $curr.kind

			if ($kind -in $skipped_record_elems) {
				continue
			}

			if ($kind -eq 'VarDecl') {
				$var_output = map_expr $curr ($nesting + 1) NoSpace

				if ($var_output.StartsWith('let ')) {
					$var_output = "global $($var_output.Substring(4))"
				} elseif ($var_output.StartsWith('mut ')) {
					$var_output = "mut global $($var_output.Substring(4))"
				}
				$output += "$(indent ($nesting + 1) NewLine)$var_output"
			} elseif ($kind -eq 'CXXRecordDecl') {
				# Skip pseudo-forward declaration
				if (-not $curr.completeDefinition) {
					continue
				}

				if ($curr.name) {
					Write-Error "Expected nested struct definitions to not have a name but got '$($curr.name)'"
					exit
				}

				$prev_anonymous_record = (map_expr $curr ($nesting + 1) NoSpace) -replace '^(let|pub)\s+=\s+', ''

				continue
			} elseif ($kind -eq 'FieldDecl') {
				[string]$field = map_expr $curr ($nesting + 1) NewLine

				$output += $field

				if ($prev_anonymous_record -ne '') {
					if ($field -notmatch ':\s*$') {
						$curr.PSObject.Properties | foreach { Write-Host $_ }
						Write-Error "Expected 'FieldDecl' after nested struct definition to not have a type but got '$field'"
						exit
					}

					$output += $prev_anonymous_record
					$prev_anonymous_record = ''
				}
			} else {
				Write-Error "Expected 'CXXRecordDecl' to contain only $(($skipped | foreach { "'$_'"}) -join ', '), 'FieldDecl' or 'VarlDecl' but found '$($_.kind)'"
				exit
			}

			$output += ','
		}

		[string]$end_line = if ($output.EndsWith(')')) { 'NoSpace' } else { 'NewLine' }
		return "$output$(indent $nesting))"
	}

	'^DeclRefExpr$' {
		return "$(indent $nesting $line)$(map_name $node.referencedDecl.name)"
	}

	'^CXXConstructExpr$' {
		[string]$output = "$(indent $nesting $line)/* cpp ctor */ init("

		[bool]$is_first = $true

		if (-not $node.inner) {
			return "$output)"
		}

		$node.inner | foreach {
			if (-not $is_first) {
				$output += ', '
			} else {
				$is_first = $false
			}

			$output += map_expr $_ ($nesting + 1) NoSpace
		}

		return "$output)"
	}

	'^(PragmaDetectMismatchDecl|PragmaCommentDecl|MayAliasAttr|PackedAttr|AlignedAttr|TypeVisibilityAttr)$' {
		# Ignore
		return $null
	}

	'^(ShuffleVectorExpr|ConvertVectorExpr)$' {
		# TODO: Replace placeholder.
		return "$(indent $nesting $line)unreachable"
	}

	'^(DefaultStmt|CaseStmt)$' {
		Write-Warning "Only Expected expression node kind '$($node.kind)' in top-level of a switch."
	}

	'^FunctionDecl$' {
		if (-not $node.name) {
			Write-Error 'Missing name for FunctionDecl'
			exit
		}

		[string]$name = map_name $node.name
		[string]$introducer = if ($nesting -eq 0) { 'pub' } else { 'let' }

		[string]$output = "$introducer $name = proc("

		[bool]$is_first = $true

		[int]$body_index = 0

		$node.inner | foreach {
			if ($_.kind -eq 'ParmVarDecl') {
				if (-not $is_first) {
					$output += ', '
				} else {
					$is_first = $false
				}

				$output += map_parameter $_.name $_.type.qualType ''

				$body_index += 1
			}
		}

		if ($body_index -eq $node.inner.Count -or $node.inner[$body_index].kind -ne 'CompoundStmt') {
			# Skip declarations, we only care about definitions right now.
			return $null
		}

		$full_type = $node.type.qualType

		$return_type = (map_type $full_type.Substring(0, $full_type.IndexOf('('))).mapped

		$body = map_expr $node.inner[$body_index] ($nesting + 1) BlockSameLine

		return "$output) -> $return_type =>$body"
	}

	# These aren't supported yet
	'^EnumDecl$' {}
	'^FunctionTemplateDecl$' {}
	'^NamespaceDecl$' {}
	'^TypeAliasDecl$' {}
	'^UsingDecl$' {}
	'^UsingShadowDecl$' {}
	'^VarTemplateDecl$' {}
	'^VarTemplatePartialSpecializationDecl$' {}
	'^VarTemplateSpecializationDecl$' {}
	'^LinkageSpecDecl$' {}
	'^ClassTemplateDecl$' {}
	'^CXXConstructorDecl$' {}
	'^CXXDestructorDecl$' {}
	'^CXXMethodDecl$' {}
	'^StaticAssertDecl$' {}
	'^IndirectFieldDecl$' {}

	default {
		Write-Error "Unknown expression node kind '$($node.kind)'"
		exit
	}
	}
}

if ($ast.GetType() -eq [string] -and $ast.ToLower().EndsWith('.json')) {
	Write-Host "Reading AST Json..."

	$ast = Get-Content $ast -Raw | ConvertFrom-Json
}

if (-not $ast.is_stripped) {
	Write-Host "Stripping AST Json..."

	strip $ast

	$ast | Add-Member -MemberType NoteProperty -Name 'is_stripped' -Value $true
}

Write-Host "Emitting lophus..."

$ast.inner | foreach { "$(map_expr $_ 0 NewLine)`n" } | where { $_ -ne "`n" }

Write-Host "Success"
