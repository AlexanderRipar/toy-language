#include "diag.hpp"

static void print_config_node(PrintSink sink, const Config* config, const TreeSchemaNode* node, u32 indent) noexcept
{
	if (node->kind == TreeSchemaNodeKind::Container)
	{
		print(sink, "%[< %]% {\n", "", indent * 2, node->name);

		for (const TreeSchemaNode& child : node->container.children)
			print_config_node(sink, config, &child, indent + 1);

		print(sink, "%[< %]}\n", "", indent * 2);
	}
	else if (node->kind == TreeSchemaNodeKind::Integer)
	{
		const s64 value = *reinterpret_cast<const s64*>(reinterpret_cast<const byte*>(config) + node->target_offset);

		print(sink, "%[< %]% = %\n", "", indent * 2, node->name, value);
	}
	else if (node->kind == TreeSchemaNodeKind::String || node->kind == TreeSchemaNodeKind::Path)
	{
		const Range<char8> value = *reinterpret_cast<const Range<char8>*>(reinterpret_cast<const byte*>(config) + node->target_offset);

		print(sink, "%[< %]% = '%'\n", "", indent * 2, node->name, value);
	}
	else if (node->kind == TreeSchemaNodeKind::Boolean)
	{
		const bool value = *reinterpret_cast<const bool*>(reinterpret_cast<const byte*>(config) + node->target_offset);

		print(sink, "%[< %]% = %\n", "", indent * 2, node->name, value);
	}
	else
	{
		ASSERT_UNREACHABLE;
	}
}

static void print_config_help_node(PrintSink sink, const Config* defaults, const TreeSchemaNode* node, u32 indent, u32 max_indent) noexcept
{
	if (node->kind == TreeSchemaNodeKind::Container)
	{
		print(sink, "%[< %]% {\n%[< %]%\n",
			"", indent * 2,
			node->name,
			"", (indent + 1) * 2,
			node->helptext
		);

		if (indent != max_indent)
		{
			for (const TreeSchemaNode& child : node->container.children)
				print_config_help_node(sink, defaults, &child, indent + 1, max_indent);
		}

		print(sink, "%[< %]}\n",
			"", indent * 2
		);
	}
	else if (node->kind == TreeSchemaNodeKind::Integer)
	{
		const s64 default_value = *reinterpret_cast<const s64*>(reinterpret_cast<const byte*>(defaults) + node->target_offset);

		print(sink, "%[< %]% {\n%[< %]%\n%[< %]type: integer\n%[< %]default: %\n",
			"", indent * 2,
			node->name,
			"", (indent + 1) * 2,
			node->helptext,
			"", (indent + 1) * 2,
			"", (indent + 1) * 2,
			default_value
		);

		if (node->integer.min != INT64_MIN)
		{
			print(sink, "%[< %]min: %\n",
				"", (indent + 1) * 2,
				node->integer.min
			);
		}

		if (node->integer.max != INT64_MAX)
		{
			print(sink, "%[< %]max: %\n",
				"", (indent + 1) * 2,
				node->integer.max
			);
		}

		print(sink, "%[< %]}\n",
			"", indent * 2
		);
	}
	else if (node->kind == TreeSchemaNodeKind::String || node->kind == TreeSchemaNodeKind::Path)
	{
		const Range<char8> default_value = *reinterpret_cast<const Range<char8>*>(reinterpret_cast<const byte*>(defaults) + node->target_offset);

		print(sink, "%[< %]% {\n%[< %]%\n%[< %]type: %\n%[< %]default: %\n%[< %]}\n",
			"", indent * 2,
			node->name,
			"", (indent + 1) * 2,
			node->helptext,
			"", (indent + 1) * 2,
			node->kind == TreeSchemaNodeKind::String ? "string" : "path",
			"", (indent + 1) * 2,
			default_value,
			"", indent * 2
		);
	}
	else if (node->kind == TreeSchemaNodeKind::Boolean)
	{
		const bool default_value = *reinterpret_cast<const bool*>(reinterpret_cast<const byte*>(defaults) + node->target_offset);

		print(sink, "%[< %]% {\n%[< %]%\n%[< %]type: bool\n%[< %]default: %\n%[< %]}\n",
			"", indent * 2,
			node->name,
			"", (indent + 1) * 2,
			node->helptext,
			"", (indent + 1) * 2,
			"", (indent + 1) * 2,
			default_value,
			"", indent * 2
		);
	}
	else
	{
		ASSERT_UNREACHABLE;
	}
}



void diag::print_config(PrintSink sink, const Config* config) noexcept
{
	print(sink, "\n### CONFIG ###\n\n");

	const TreeSchemaNode* const root = config_schema();

	for (const TreeSchemaNode& node : root->container.children)
		print_config_node(sink, config, &node, 0);
}

void diag::print_config_help(PrintSink sink, u32 depth) noexcept
{
	print(sink, "config parameters:\n");

	const Config defaults{};

	const TreeSchemaNode* const root = config_schema();

	for (const TreeSchemaNode& node : root->container.children)
		print_config_help_node(sink, &defaults, &node, 0, depth == 0 ? UINT32_MAX : depth);
}
