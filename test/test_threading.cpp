#include "tests.hpp"
#include "helpers.hpp"
#include "../threading.hpp"
#include "../range.hpp"
#include "../hash.hpp"

#include <cstdlib>
#include <atomic>

namespace ringbuffer_tests
{
	struct ThreadProcArgs
	{
		ThreadsafeRingBufferHeader<u32> header;

		u32* queue;

		u32 capacity;

		u32 operation_count;

		std::atomic<u64> accumulated_result;
	};

	static void enqueue_threadproc(ThreadProcArgs* args, u32 thread_id, [[maybe_unused]] u32 thread_count) noexcept
	{
		for (u32 i = 0; i != args->operation_count; ++i)
			CHECK_EQ(args->header.enqueue(args->queue, args->capacity, i + thread_id * args->operation_count), true, "Enqueue on non-full queue succeeds");
	}

	static void dequeue_threadproc(ThreadProcArgs* args, [[maybe_unused]] u32 thread_id, [[maybe_unused]] u32 thread_count) noexcept
	{
		u64 sum = 0;

		for (u32 i = 0; i != args->operation_count; ++i)
		{
			u32 entry;

			CHECK_EQ(args->header.dequeue(args->queue, args->capacity, &entry), true, "dequeue on non-empty queue succeeds");

			sum += entry;
		}

		args->accumulated_result.fetch_add(sum, std::memory_order_relaxed);
	}

	static void enqdeq_threadproc(ThreadProcArgs* args, [[maybe_unused]] u32 thread_id, [[maybe_unused]] u32 thread_count) noexcept
	{
		u32 dequeued_count = 0;
		for (u32 rep = 0; rep != args->operation_count; ++rep)
		{
			CHECK_EQ(args->header.enqueue(args->queue, args->capacity, 1), true, "enqueue called on a non-full queue returns true");

			u32 unused;

			if (args->header.dequeue(args->queue, args->capacity, &unused))
				dequeued_count += 1;
		}

		args->accumulated_result.fetch_add(dequeued_count, std::memory_order_relaxed);
	}

	namespace exclusive
	{
		static void dequeue_on_empty_buffer_returns_false() noexcept
		{
			ThreadsafeRingBufferHeader<u32> header;

			u32 buffer[8];

			header.init();

			u32 deqeued_element;

			CHECK_EQ(header.dequeue(buffer, static_cast<u32>(array_count(buffer)), &deqeued_element), false, "dequeue on empty buffer returns false");
		}

		static void enqueue_then_dequeue_returns_true_and_enqueued_element() noexcept
		{
			ThreadsafeRingBufferHeader<u32> header;

			u32 buffer[8];

			header.init();

			CHECK_EQ(header.enqueue(buffer, static_cast<u32>(array_count(buffer)), 0xFEEDBEEF), true, "enqueue on buffer with free space returns true");

			u32 deqeued_element;

			CHECK_EQ(header.dequeue(buffer, static_cast<u32>(array_count(buffer)), &deqeued_element), true, "dequeue on non-empty buffer returns true");

			CHECK_EQ(deqeued_element, 0xFEEDBEEF, "dequeued element has the expected value");
		}

		static void enqueue_on_full_buffer_returns_false() noexcept
		{
			ThreadsafeRingBufferHeader<u32> header;

			u32 buffer[8];

			header.init();

			for (u32 i = 0; i != static_cast<u32>(array_count(buffer)); ++i)
				CHECK_EQ(header.enqueue(buffer, static_cast<u32>(array_count(buffer)), i), true, "enqueue on buffer with free space returns true");

			CHECK_EQ(header.enqueue(buffer, static_cast<u32>(array_count(buffer)), 0xDEADBEEF), false, "enqueue on full buffer returns false");
		}

		static void dequeue_returns_elements_in_fifo_order() noexcept
		{
			ThreadsafeRingBufferHeader<u32> header;

			u32 buffer[8];

			header.init();

			for (u32 i = 0; i != static_cast<u32>(array_count(buffer)); ++i)
				CHECK_EQ(header.enqueue(buffer, static_cast<u32>(array_count(buffer)), i), true, "enqueue on buffer with free space returns true");

			for (u32 i = 0; i != static_cast<u32>(array_count(buffer)); ++i)
			{
				u32 deqeued_element;

				CHECK_EQ(header.dequeue(buffer, static_cast<u32>(array_count(buffer)), &deqeued_element), true, "dequeue on non-empty buffer returns true");

				CHECK_EQ(deqeued_element, i, "nth dequeued element is nth enqueued element");
			}
		}
	}

	namespace parallel
	{
		static void enqueues_do_not_loose_entries() noexcept
		{
			static constexpr u32 ENQUEUE_COUNT_PER_THREAD = 8192;

			static constexpr u32 THREAD_COUNT = 16;

			static constexpr u32 QUEUE_CAPACITY = next_pow2(ENQUEUE_COUNT_PER_THREAD * THREAD_COUNT);

			ThreadProcArgs args;
			args.header.init();
			args.operation_count = ENQUEUE_COUNT_PER_THREAD;
			args.capacity = QUEUE_CAPACITY;
			args.queue = static_cast<u32*>(malloc(QUEUE_CAPACITY * sizeof(u32)));

			run_on_threads_and_wait(THREAD_COUNT, enqueue_threadproc, &args);

			for (u32 i = 0; i != ENQUEUE_COUNT_PER_THREAD * THREAD_COUNT; ++i)
			{
				u32 unused;

				CHECK_EQ(args.header.dequeue(args.queue, args.capacity, &unused), true, "Dequeue succeeds on non-empty queue");
			}

			u32 unused;

			CHECK_EQ(args.header.dequeue(args.queue, args.capacity, &unused), false, "Dequeue returns false on empty queue");
		}

		static void dequeues_do_not_loose_entries() noexcept
		{
			static constexpr u32 ENQUEUE_COUNT_PER_THREAD = 8192;

			static constexpr u32 THREAD_COUNT = 16;

			static constexpr u32 QUEUE_CAPACITY = next_pow2(ENQUEUE_COUNT_PER_THREAD * THREAD_COUNT);

			ThreadProcArgs args;
			args.header.init();
			args.operation_count = ENQUEUE_COUNT_PER_THREAD;
			args.capacity = QUEUE_CAPACITY;
			args.queue = static_cast<u32*>(malloc(QUEUE_CAPACITY * sizeof(u32)));
			args.accumulated_result.store(0, std::memory_order_relaxed);

			for (u32 i = 0; i != ENQUEUE_COUNT_PER_THREAD * THREAD_COUNT; ++i)
				CHECK_EQ(args.header.enqueue(args.queue, args.capacity, i), true, "Enqueue returns true on a non-full queue");

			run_on_threads_and_wait(THREAD_COUNT, dequeue_threadproc, &args);

			u32 unused;

			CHECK_EQ(args.header.dequeue(args.queue, args.capacity, &unused), false, "Dequeue returns false on empty queue");

			static constexpr u64 N = ENQUEUE_COUNT_PER_THREAD * THREAD_COUNT - 1;

			static constexpr u64 EXPECTED = (N * N + N) / 2;

			CHECK_EQ(args.accumulated_result.load(std::memory_order_relaxed), EXPECTED, "Accumulated dequeued results match accumulated enqueued results");
		}

		static void enqueues_and_dequeues_do_not_loose_entries() noexcept
		{
			static constexpr u32 ENQDEQ_COUNT_PER_THREAD = 8192;

			static constexpr u32 THREAD_COUNT = 16;

			static constexpr u32 QUEUE_CAPACITY = next_pow2(ENQDEQ_COUNT_PER_THREAD * THREAD_COUNT);

			ThreadProcArgs args;
			args.header.init();
			args.operation_count = ENQDEQ_COUNT_PER_THREAD;
			args.capacity = QUEUE_CAPACITY;
			args.queue = static_cast<u32*>(malloc(QUEUE_CAPACITY * sizeof(u32)));
			args.accumulated_result.store(0, std::memory_order_relaxed);

			run_on_threads_and_wait(THREAD_COUNT, enqdeq_threadproc, &args);

			u32 leftover_dequeue_count = 0;

			u32 unused;

			while (args.header.dequeue(args.queue, args.capacity, &unused))
				leftover_dequeue_count += 1;

			CHECK_EQ(args.accumulated_result.load(std::memory_order_relaxed) + leftover_dequeue_count, ENQDEQ_COUNT_PER_THREAD * THREAD_COUNT, "Count of dequeues performed concurrent to enqueues, plus dequeues left over afterwards equals count of enqueues");
		}
	}
}

static void ringbuffer() noexcept
{
	using namespace ringbuffer_tests;

	exclusive::dequeue_on_empty_buffer_returns_false();

	exclusive::enqueue_then_dequeue_returns_true_and_enqueued_element();

	exclusive::enqueue_on_full_buffer_returns_false();

	exclusive::dequeue_returns_elements_in_fifo_order();

	parallel::enqueues_do_not_loose_entries();

	parallel::dequeues_do_not_loose_entries();

	parallel::enqueues_and_dequeues_do_not_loose_entries();
}

namespace indexstacklist_tests
{
	struct Node
	{
		u32 data;

		u32 next;
	};

	struct PushThreadProcArgs
	{
		u32 node_count_per_thread;

		Node* nodes;
		
		ThreadsafeIndexStackListHeader<Node, offsetof(Node, next)> stack;
	};

	struct PopThreadProcArgs
	{
		std::atomic<u32> popped_node_count;

		Node* nodes;

		ThreadsafeIndexStackListHeader<Node, offsetof(Node, next)> stack;
	};

	struct PushAndPopThreadProcArgs
	{
		u32 iteration_count;

		Node* nodes;

		ThreadsafeIndexStackListHeader<Node, offsetof(Node, next)> stack; 
	};

	static void push_parallel_threadproc(PushThreadProcArgs* args, u32 thread_id, [[maybe_unused]] u32 thread_count) noexcept
	{
		for (u32 i = thread_id * args->node_count_per_thread; i != (thread_id + 1) * args->node_count_per_thread; ++i)
			args->stack.push(args->nodes, i);
	}

	static void pop_parallel_threadproc(PopThreadProcArgs* args, [[maybe_unused]] u32 thread_id, [[maybe_unused]] u32 thread_count) noexcept
	{
		u32 popped_node_count = 0;

		while (args->stack.pop(args->nodes) != nullptr)
			popped_node_count += 1;

		args->popped_node_count.fetch_add(popped_node_count, std::memory_order_relaxed);
	}

	static void push_and_pop_parallel_threadproc(PushAndPopThreadProcArgs* args, [[maybe_unused]] u32 thread_id, [[maybe_unused]] u32 thread_count) noexcept
	{
		Node* popped_list = nullptr;

		for (u32 iter = 0; iter != args->iteration_count; ++iter)
		{
			while (true)
			{
				Node* const popped = args->stack.pop(args->nodes);

				if (popped == nullptr)
					break;

				popped->next = static_cast<u32>(popped_list == nullptr ? ~0u : popped_list - args->nodes);

				popped_list = popped;
			}

			while (popped_list != nullptr)
			{
				const u32 popped_next = popped_list->next;

				args->stack.push(args->nodes, static_cast<u32>(popped_list - args->nodes));

				popped_list = popped_next == ~0u ? nullptr : args->nodes + popped_next;
			}
		}
	}

	namespace exclusive
	{
		static void pop_on_empty_list_returns_null() noexcept
		{
			ThreadsafeIndexStackListHeader<Node, offsetof(Node, next)> stack;

			Node dummy_node[1];

			stack.init();

			CHECK_EQ(stack.pop(dummy_node), nullptr, "Popping an empty stack returns nullptr");

			CHECK_EQ(stack.pop(dummy_node), nullptr, "Popping an empty stack a second time still returns nullptr");
		}

		static void init_with_array_then_pop_returns_all_elements() noexcept
		{
			ThreadsafeIndexStackListHeader<Node, offsetof(Node, next)> stack;

			Node nodes[512];

			for (u32 i = 0; i != array_count(nodes); ++i)
				nodes[i].data = i;

			stack.init(nodes, static_cast<u32>(array_count(nodes)));

			u32 popped_node_count = 0;

			while (true)
			{
				const Node* node = stack.pop(nodes);

				if (node == nullptr)
					break;

				CHECK_EQ(node->data, popped_node_count, "init with array initializes the stack with the array's first element on top");

				popped_node_count += 1;
			}

			CHECK_EQ(popped_node_count, array_count(nodes), "Expected number of nodes are popped after init with array");
		}

		static void push_then_pop_returns_pushed_element() noexcept
		{
			ThreadsafeIndexStackListHeader<Node, offsetof(Node, next)> stack;

			Node node[1];

			stack.init();

			stack.push(node, 0);

			CHECK_EQ(stack.pop(node), node, "Pop returns previously pushed element");

			CHECK_EQ(stack.pop(node), nullptr, "Pop after popping all elements returns nullptr");
		}

		static void push_unsafe_then_pop_returns_pushed_element() noexcept
		{
			ThreadsafeIndexStackListHeader<Node, offsetof(Node, next)> stack;

			Node node[1];

			stack.init();

			stack.push_unsafe(node, 0);

			CHECK_EQ(stack.pop(node), node, "Pop returns previously (unsafely) pushed element");

			CHECK_EQ(stack.pop(node), nullptr, "Pop after popping all elements returns nullptr");
		}

		static void push_then_pop_unsafe_returns_pushed_element() noexcept
		{
			ThreadsafeIndexStackListHeader<Node, offsetof(Node, next)> stack;

			Node node[1];

			stack.init();

			stack.push(node, 0);

			CHECK_EQ(stack.pop_unsafe(node), node, "Pop returns previously (unsafely) pushed element");

			CHECK_EQ(stack.pop_unsafe(node), nullptr, "Pop after popping all elements returns nullptr");
		}
	}

	namespace parallel
	{
		static void push_does_not_loose_nodes() noexcept
		{
			static constexpr u32 NODE_COUNT_PER_THREAD = 65536;

			static constexpr u32 THREAD_COUNT = 8;

			PushThreadProcArgs args;

			args.node_count_per_thread = NODE_COUNT_PER_THREAD;

			args.stack.init();

			CHECK_NE(args.nodes = static_cast<Node*>(malloc(NODE_COUNT_PER_THREAD * THREAD_COUNT * sizeof(Node))), nullptr, "malloc succeeds");

			run_on_threads_and_wait(THREAD_COUNT, push_parallel_threadproc, &args);

			u32 pushed_node_count = 0;

			while (args.stack.pop(args.nodes) != nullptr)
				pushed_node_count += 1;

			CHECK_EQ(pushed_node_count, NODE_COUNT_PER_THREAD * THREAD_COUNT, "Number of sequentially popped nodes is equal to nodes pushed in parallel");

			free(args.nodes);
		}

		static void pop_does_not_duplicate_nodes() noexcept
		{
			static constexpr u32 NODE_COUNT_PER_THREAD = 65536;

			static constexpr u32 THREAD_COUNT = 8;

			PopThreadProcArgs args;

			args.popped_node_count.store(0, std::memory_order_relaxed);

			args.stack.init();

			CHECK_NE(args.nodes = static_cast<Node*>(malloc(NODE_COUNT_PER_THREAD * THREAD_COUNT * sizeof(Node))), nullptr, "malloc succeeds");

			for (u32 i = 0; i != NODE_COUNT_PER_THREAD * THREAD_COUNT; ++i)
				args.stack.push(args.nodes, i);

			run_on_threads_and_wait(THREAD_COUNT, pop_parallel_threadproc, &args);

			CHECK_EQ(args.popped_node_count.load(std::memory_order_relaxed), NODE_COUNT_PER_THREAD * THREAD_COUNT, "Number of sequentially pushed nodes is equal to nodes popped in parallel");

			free(args.nodes);
		}

		static void push_and_pop_does_not_drop_nodes() noexcept
		{
			static constexpr u32 THREAD_COUNT = 8;

			static constexpr u32 TOTAL_NODE_COUNT = 30'000;

			static constexpr u32 THREAD_ITERATION_COUNT = 10;

			PushAndPopThreadProcArgs args;

			args.iteration_count = THREAD_ITERATION_COUNT;

			CHECK_NE(args.nodes = static_cast<Node*>(malloc(TOTAL_NODE_COUNT * sizeof(Node))), nullptr, "malloc succeeds");

			args.stack.init(args.nodes, TOTAL_NODE_COUNT);

			run_on_threads_and_wait(THREAD_COUNT, push_and_pop_parallel_threadproc, &args);

			u32 popped_node_count = 0;

			while (args.stack.pop(args.nodes) != nullptr)
				popped_node_count += 1;

			CHECK_EQ(popped_node_count, TOTAL_NODE_COUNT, "Popping and re-pushing batches of nodes in parallel does not loose any nodes");
		}
	}
}

static void indexstacklist() noexcept
{
	using namespace indexstacklist_tests;

	exclusive::pop_on_empty_list_returns_null();

	exclusive::init_with_array_then_pop_returns_all_elements();

	exclusive::push_then_pop_returns_pushed_element();

	exclusive::push_unsafe_then_pop_returns_pushed_element();

	exclusive::push_then_pop_unsafe_returns_pushed_element();

	parallel::push_does_not_loose_nodes();

	parallel::pop_does_not_duplicate_nodes();

	parallel::push_and_pop_does_not_drop_nodes();
}

namespace stridedindexstacklist_tests
{
	struct Node
	{
		u32 data;

		u32 next;
	};

	struct PushThreadProcArgs
	{
		u32 node_count_per_thread;

		Node* nodes;
		
		ThreadsafeStridedIndexStackListHeader<Node, offsetof(Node, next)> stack;
	};

	struct PopThreadProcArgs
	{
		std::atomic<u32> popped_node_count;

		Node* nodes;

		ThreadsafeStridedIndexStackListHeader<Node, offsetof(Node, next)> stack;
	};

	struct PushAndPopThreadProcArgs
	{
		u32 iteration_count;

		Node* nodes;

		ThreadsafeStridedIndexStackListHeader<Node, offsetof(Node, next)> stack; 
	};

	static void push_parallel_threadproc(PushThreadProcArgs* args, u32 thread_id, [[maybe_unused]] u32 thread_count) noexcept
	{
		for (u32 i = thread_id * args->node_count_per_thread; i != (thread_id + 1) * args->node_count_per_thread; ++i)
			args->stack.push(args->nodes, sizeof(*args->nodes), i);
	}

	static void pop_parallel_threadproc(PopThreadProcArgs* args, [[maybe_unused]] u32 thread_id, [[maybe_unused]] u32 thread_count) noexcept
	{
		u32 popped_node_count = 0;

		while (args->stack.pop(args->nodes, sizeof(Node)) != nullptr)
			popped_node_count += 1;

		args->popped_node_count.fetch_add(popped_node_count, std::memory_order_relaxed);
	}

	static void push_and_pop_parallel_threadproc(PushAndPopThreadProcArgs* args, [[maybe_unused]] u32 thread_id, [[maybe_unused]] u32 thread_count) noexcept
	{
		Node* popped_list = nullptr;

		for (u32 iter = 0; iter != args->iteration_count; ++iter)
		{
			while (true)
			{
				Node* const popped = args->stack.pop(args->nodes, sizeof(Node));

				if (popped == nullptr)
					break;

				popped->next = static_cast<u32>(popped_list == nullptr ? ~0u : popped_list - args->nodes);

				popped_list = popped;
			}

			while (popped_list != nullptr)
			{
				const u32 popped_next = popped_list->next;

				args->stack.push(args->nodes, sizeof(Node), static_cast<u32>(popped_list - args->nodes));

				popped_list = popped_next == ~0u ? nullptr : args->nodes + popped_next;
			}
		}
	}

	namespace exclusive
	{
		static void pop_on_empty_list_returns_null() noexcept
		{
			ThreadsafeStridedIndexStackListHeader<Node, offsetof(Node, next)> stack;

			Node dummy_node[1];

			stack.init();

			CHECK_EQ(stack.pop(dummy_node, sizeof(*dummy_node)), nullptr, "Popping an empty stack returns nullptr");

			CHECK_EQ(stack.pop(dummy_node, sizeof(*dummy_node)), nullptr, "Popping an empty stack a second time still returns nullptr");
		}

		static void init_with_array_then_pop_returns_all_elements() noexcept
		{
			ThreadsafeStridedIndexStackListHeader<Node, offsetof(Node, next)> stack;

			Node nodes[512];

			for (u32 i = 0; i != array_count(nodes); ++i)
				nodes[i].data = i;

			stack.init(nodes, sizeof(Node), static_cast<u32>(array_count(nodes)));

			u32 popped_node_count = 0;

			while (true)
			{
				const Node* node = stack.pop(nodes, sizeof(Node));

				if (node == nullptr)
					break;

				CHECK_EQ(node->data, popped_node_count, "init with array initializes the stack with the array's first element on top");

				popped_node_count += 1;
			}

			CHECK_EQ(popped_node_count, array_count(nodes), "Expected number of nodes are popped after init with array");
		}

		static void init_with_double_stride_then_pop_returns_every_second_element() noexcept
		{
			ThreadsafeStridedIndexStackListHeader<Node, offsetof(Node, next)> stack;

			Node nodes[512];

			constexpr u32 ADJ_COUNT = static_cast<u32>(array_count(nodes) / 2);

			constexpr u32 STRIDE = sizeof(Node) * 2;

			for (u32 i = 0; i != array_count(nodes); ++i)
				nodes[i].data = i;

			stack.init(nodes, STRIDE, ADJ_COUNT);

			u32 popped_node_count = 0;

			while (true)
			{
				const Node* node = stack.pop(nodes, STRIDE);

				if (node == nullptr)
					break;

				CHECK_EQ(node->data, popped_node_count * 2, "init with array and doubled stride initializes the stack with every other element and the array's first element on top");

				popped_node_count += 1;
			}

			CHECK_EQ(popped_node_count, ADJ_COUNT, "Expected number of nodes are popped after init with array and doubled stride");
		}

		static void push_then_pop_returns_pushed_element() noexcept
		{
			ThreadsafeStridedIndexStackListHeader<Node, offsetof(Node, next)> stack;

			Node node[1];

			stack.init();

			stack.push(node, sizeof(node), 0);

			CHECK_EQ(stack.pop(node, sizeof(*node)), node, "Pop returns previously pushed element");

			CHECK_EQ(stack.pop(node, sizeof(*node)), nullptr, "Pop after popping all elements returns nullptr");
		}

		static void pushes_then_pops_with_doubled_stride_return_pushed_elements() noexcept
		{
			ThreadsafeStridedIndexStackListHeader<Node, offsetof(Node, next)> stack;

			Node nodes[4];

			constexpr u32 STRIDE = sizeof(Node) * 2;

			stack.init();

			stack.push(nodes, STRIDE, 0);

			stack.push(nodes, STRIDE, 1);

			CHECK_EQ(stack.pop(nodes, STRIDE), nodes + 2, "pop after two pushes returns later element");

			CHECK_EQ(stack.pop(nodes, STRIDE), nodes, "Second pop after two pushes returns earlier element");

			CHECK_EQ(stack.pop(nodes, STRIDE), nullptr, "Third pop after two pushes returns nullptr");
		}

		static void push_unsafe_then_pop_returns_pushed_element() noexcept
		{
			ThreadsafeStridedIndexStackListHeader<Node, offsetof(Node, next)> stack;

			Node node[1];

			stack.init();

			stack.push_unsafe(node, sizeof(Node), 0);

			CHECK_EQ(stack.pop(node, sizeof(*node)), node, "Pop returns previously (unsafely) pushed element");

			CHECK_EQ(stack.pop(node, sizeof(*node)), nullptr, "Pop after popping all elements returns nullptr");
		}

		static void push_then_pop_unsafe_returns_pushed_element() noexcept
		{
			ThreadsafeStridedIndexStackListHeader<Node, offsetof(Node, next)> stack;

			Node node[1];

			stack.init();

			stack.push(node, sizeof(Node), 0);

			CHECK_EQ(stack.pop_unsafe(node, sizeof(*node)), node, "Pop returns previously (unsafely) pushed element");

			CHECK_EQ(stack.pop_unsafe(node, sizeof(*node)), nullptr, "Pop after popping all elements returns nullptr");
		}

		static void unsafe_pushes_and_pops_with_doubled_stride_return_pushed_elements() noexcept
		{
			ThreadsafeStridedIndexStackListHeader<Node, offsetof(Node, next)> stack;

			Node nodes[4];

			constexpr u32 STRIDE = sizeof(Node) * 2;

			stack.init();

			stack.push_unsafe(nodes, STRIDE, 0);

			stack.push_unsafe(nodes, STRIDE, 1);

			CHECK_EQ(stack.pop_unsafe(nodes, STRIDE), nodes + 2, "pop_unsafe after two push_unsafes returns later element");

			CHECK_EQ(stack.pop_unsafe(nodes, STRIDE), nodes, "Second pop_unsafe after two push_unsafes returns earlier element");

			CHECK_EQ(stack.pop_unsafe(nodes, STRIDE), nullptr, "Third pop_unsafe after two push_unsafes returns nullptr");
		}
	}

	namespace parallel
	{
		static void push_does_not_loose_nodes() noexcept
		{
			static constexpr u32 NODE_COUNT_PER_THREAD = 65536;

			static constexpr u32 THREAD_COUNT = 8;

			PushThreadProcArgs args;

			args.node_count_per_thread = NODE_COUNT_PER_THREAD;

			args.stack.init();

			CHECK_NE(args.nodes = static_cast<Node*>(malloc(NODE_COUNT_PER_THREAD * THREAD_COUNT * sizeof(Node))), nullptr, "malloc succeeds");

			run_on_threads_and_wait(THREAD_COUNT, push_parallel_threadproc, &args);

			u32 pushed_node_count = 0;

			while (args.stack.pop(args.nodes, sizeof(Node)) != nullptr)
				pushed_node_count += 1;

			CHECK_EQ(pushed_node_count, NODE_COUNT_PER_THREAD * THREAD_COUNT, "Number of sequentially popped nodes is equal to nodes pushed in parallel");

			free(args.nodes);
		}

		static void pop_does_not_duplicate_nodes() noexcept
		{
			static constexpr u32 NODE_COUNT_PER_THREAD = 65536;

			static constexpr u32 THREAD_COUNT = 8;

			PopThreadProcArgs args;

			args.popped_node_count.store(0, std::memory_order_relaxed);

			args.stack.init();

			CHECK_NE(args.nodes = static_cast<Node*>(malloc(NODE_COUNT_PER_THREAD * THREAD_COUNT * sizeof(Node))), nullptr, "malloc succeeds");

			for (u32 i = 0; i != NODE_COUNT_PER_THREAD * THREAD_COUNT; ++i)
				args.stack.push(args.nodes, sizeof(Node), i);

			run_on_threads_and_wait(THREAD_COUNT, pop_parallel_threadproc, &args);

			CHECK_EQ(args.popped_node_count.load(std::memory_order_relaxed), NODE_COUNT_PER_THREAD * THREAD_COUNT, "Number of sequentially pushed nodes is equal to nodes popped in parallel");

			free(args.nodes);
		}

		static void push_and_pop_does_not_drop_nodes() noexcept
		{
			static constexpr u32 THREAD_COUNT = 8;

			static constexpr u32 TOTAL_NODE_COUNT = 30'000;

			static constexpr u32 THREAD_ITERATION_COUNT = 10;

			PushAndPopThreadProcArgs args;

			args.iteration_count = THREAD_ITERATION_COUNT;

			CHECK_NE(args.nodes = static_cast<Node*>(malloc(TOTAL_NODE_COUNT * sizeof(Node))), nullptr, "malloc succeeds");

			args.stack.init(args.nodes, sizeof(Node), TOTAL_NODE_COUNT);

			run_on_threads_and_wait(THREAD_COUNT, push_and_pop_parallel_threadproc, &args);

			u32 popped_node_count = 0;

			while (args.stack.pop(args.nodes, sizeof(Node)) != nullptr)
				popped_node_count += 1;

			CHECK_EQ(popped_node_count, TOTAL_NODE_COUNT, "Popping and re-pushing batches of nodes in parallel does not loose any nodes");
		}
	}
}

static void stridedindexstacklist() noexcept
{
	using namespace stridedindexstacklist_tests;

	exclusive::pop_on_empty_list_returns_null();

	exclusive::init_with_array_then_pop_returns_all_elements();

	exclusive::init_with_double_stride_then_pop_returns_every_second_element();

	exclusive::push_then_pop_returns_pushed_element();

	exclusive::pushes_then_pops_with_doubled_stride_return_pushed_elements();

	exclusive::push_unsafe_then_pop_returns_pushed_element();

	exclusive::push_then_pop_unsafe_returns_pushed_element();

	exclusive::unsafe_pushes_and_pops_with_doubled_stride_return_pushed_elements();

	parallel::push_does_not_loose_nodes();

	parallel::pop_does_not_duplicate_nodes();

	parallel::push_and_pop_does_not_drop_nodes();
}

namespace map_tests
{
	template<typename T>
	struct KeyGenerator
	{
		void init(u32 per_thread_insertion_count, bool duplicate_inserts, u32 thread_id) noexcept
		{
			static_assert(false, "KeyGenerator created with unsupported type");
		}
	};

	template<>
	struct KeyGenerator<u32>
	{
		u32 m_curr;

		u32 m_end;

		void init(u32 per_thread_insertion_count, bool duplicate_inserts, u32 thread_id) noexcept
		{
			if (duplicate_inserts)
			{
				m_curr = 0;

				m_end = per_thread_insertion_count;
			}
			else
			{
				m_curr = per_thread_insertion_count * thread_id;

				m_end = per_thread_insertion_count * (thread_id + 1);
			}
		}

		bool has_next() const noexcept
		{
			return m_curr != m_end;
		}

		u32 next() noexcept
		{
			return m_curr++;
		}
	};

	template<>
	struct KeyGenerator<Range<char8>>
	{
		u32 m_curr;

		u32 m_end;

		u32 m_extra_bytes;

		u32 m_unq;

		char8 m_extra[512];

		void init(u32 per_thread_insertion_count, bool duplicate_inserts, u32 thread_id) noexcept
		{
			m_curr = 0;

			m_end = per_thread_insertion_count;

			m_extra_bytes = 0;

			memset(m_extra, 0, sizeof(m_extra));

			m_unq = duplicate_inserts ? 0 : per_thread_insertion_count * thread_id;
		}

		bool has_next() const noexcept
		{
			return m_curr != m_end;
		}

		Range<char8> next() noexcept
		{
			const u32 extra_bytes = m_extra_bytes;

			m_extra_bytes = (extra_bytes + 1) % array_count(m_extra);

			m_curr += 1;

			m_unq += 1;

			return Range{ reinterpret_cast<char8*>(&m_unq), sizeof(m_unq) + extra_bytes };
		}
	};

	template<typename Key, typename Value>
	struct InsertThreadProcArgs
	{
		ThreadsafeMap2<Key, Value>* map;

		u32 insertion_count;

		bool duplicate_insertions;
	};

	static u32 hash_key(u32 key) noexcept
	{
		return fnv1a(Range{ &key, 1 }.as_byte_range());
	}

	static u32 hash_key(Range<char8> key) noexcept
	{
		return fnv1a(key.as_byte_range());
	}

	template<typename Key, typename Value>
	static void init_standard_map(ThreadsafeMap2<Key, Value>* out_map, MemoryRegion* out_region)
	{
		ThreadsafeMap2<Key, Value>::InitInfo info;
		info.thread_count = 16;
		info.map.reserve_count = 1u << 18;
		info.map.initial_commit_count = 1u << 12;
		info.map.max_insertion_distance = 1024;
		info.store.reserve_strides = 1u << 18;
		info.store.per_thread_commit_increment_strides = 1u << 12;
		info.store.per_thread_initial_commit_strides = 1u << 12;

		const u64 required_bytes = out_map->required_bytes(info);

		CHECK_EQ(out_region->init(required_bytes), true, "MemoryRegion.init succeeds");

		MemorySubregion memory = out_region->subregion(0, required_bytes);

		CHECK_EQ(out_map->init(info, memory), true, "ThreadsafeMap.init succeeds");
	}

	template<typename Key, typename Value>
	static void insert_thread_proc(InsertThreadProcArgs<Key, Value>* args, u32 thread_id, [[maybe_unused]] u32 thread_count) noexcept
	{
		KeyGenerator<Key> gen;

		gen.init(args->insertion_count, args->duplicate_insertions, thread_id);

		while (gen.has_next())
		{
			Key key = gen.next();

			const u32 hash = hash_key(key);

			bool is_new;

			const u32 index = args->map->index_from(thread_id, key, hash, &is_new);

			if (!args->duplicate_insertions)
				CHECK_EQ(is_new, true, "ThreadsafeMap.index_from sets *opt_is_new to true when called with a new key");

			bool reinsert_is_new;

			const u32 reinsert_index = args->map->index_from(thread_id, key, hash, &reinsert_is_new);

			// @TODO: This fails *very* sporadically
			CHECK_EQ(reinsert_is_new, false, "ThreadsafeMap.index_from sets *opt_is_new to false when called with a key that has just been inserted");

			CHECK_EQ(index, reinsert_index, "ThreadsafeMap.index_from returns the same index when called with the same key");

			const Value* value = args->map->value_from(index);

			CHECK_EQ(value->equal_to_key(key, hash), true, "ThreadsafeMap.value_from returns the value associated with the correct key");
		}

		KeyGenerator<Key> gen2;

		gen2.init(args->insertion_count, args->duplicate_insertions, thread_id);

		while(gen2.has_next())
		{
			Key key = gen2.next();

			const u32 hash = hash_key(key);

			bool is_new;

			const u32 index = args->map->index_from(thread_id, key, hash, &is_new);

			CHECK_EQ(is_new, false, "ThreadsafeMap.index_from sets *opt_is_new to false when reinserting a key with other insertions inbetween");

			const Value* value = args->map->value_from(index);

			CHECK_EQ(value->equal_to_key(key, hash), true, "ThreadsafeMap.value_from returns the value associated with the correct key");
		}
	}

	struct FixedSizeValue
	{
		u32 m_hash;

		u32 m_key;

		u32 m_next;

		void init(u32 key, u32 key_hash) noexcept
		{
			m_hash = key_hash;

			m_key = key;
		}

		static constexpr u32 stride() noexcept
		{
			constexpr u32 stride_ = next_pow2(static_cast<u32>(sizeof(FixedSizeValue)));

			return stride_;
		}

		static u32 get_required_strides([[maybe_unused]] u32 key) noexcept
		{
			return 1;
		}

		u32 get_used_strides() noexcept
		{
			return 1;
		}

		u32 get_hash() const noexcept
		{
			return m_hash;
		}

		bool equal_to_key(u32 key, [[maybe_unused]] u32 key_hash) const noexcept
		{
			return m_key == key;
		}

		void set_next(u32 index) noexcept
		{
			m_next = index;
		}

		u32 get_next() const noexcept
		{
			return m_next;
		}
	};

	struct VariableSizeValue
	{
		u32 m_hash;

		u32 m_next;

		u16 m_key_bytes;

		#pragma warning(push)
		#pragma warning(disable : 4200) // nonstandard extension used: zero-sized array in struct/union
		char8 m_key[];
		#pragma warning(pop)

		void init(Range<char8> key, u32 key_hash) noexcept
		{
			m_hash = key_hash;

			ASSERT_OR_EXIT(key.count() <= UINT16_MAX);

			m_key_bytes = static_cast<u16>(key.count());

			memcpy(m_key, key.begin(), key.count());
		}

		static constexpr u32 stride() noexcept
		{
			return 8;
		}

		static u32 get_required_strides(Range<char8> key) noexcept
		{
			return static_cast<u32>((offsetof(VariableSizeValue, m_key) + key.count() + stride() - 1) / stride());
		}

		u32 get_used_strides() const noexcept
		{
			return static_cast<u32>((offsetof(VariableSizeValue, m_key) + m_key_bytes + stride() - 1) / stride());
		}

		u32 get_hash() const noexcept
		{
			return m_hash;
		}

		bool equal_to_key(Range<char8> key, u32 key_hash) const noexcept
		{
			return m_hash == key_hash && key.count() == m_key_bytes && memcmp(key.begin(), m_key, m_key_bytes) == 0;
		}

		void set_next(u32 index) noexcept
		{
			m_next = index;
		}

		u32 get_next() const noexcept
		{
			return m_next;
		}
	};

	namespace init
	{
		static void success_on_normal() noexcept
		{
			ThreadsafeMap2<u32, FixedSizeValue> map;

			decltype(map)::InitInfo info;
			info.thread_count = 16;
			info.map.reserve_count = 1u << 18;
			info.map.initial_commit_count = 1u << 12;
			info.map.max_insertion_distance = 1024;
			info.store.reserve_strides = 1u << 20;
			info.store.per_thread_commit_increment_strides = 1u << 12;
			info.store.per_thread_initial_commit_strides = 1u << 14;

			const u64 required_bytes = map.required_bytes(info);

			MemoryRegion region;

			CHECK_EQ(region.init(required_bytes), true, "MemoryRegion.init succeeds");

			MemorySubregion memory = region.subregion(0, required_bytes);

			CHECK_EQ(map.init(info, memory), true, "ThreadsafeMap.init succeeds with medium parameters");

			CHECK_EQ(region.deinit(), true, "MemoryRegion.deinit succeeds after successful initialization");
		}

		static void success_on_small() noexcept
		{
			ThreadsafeMap2<u32, FixedSizeValue> map;

			decltype(map)::InitInfo info;
			info.thread_count = 1;
			info.map.reserve_count = 4096;
			info.map.initial_commit_count = 4096;
			info.map.max_insertion_distance = 1024;
			info.store.reserve_strides = 4096;
			info.store.per_thread_commit_increment_strides = 4096;
			info.store.per_thread_initial_commit_strides = 4096;

			const u64 required_bytes = map.required_bytes(info);

			MemoryRegion region;

			CHECK_EQ(region.init(required_bytes), true, "MemoryRegion.init succeeds");

			MemorySubregion memory = region.subregion(0, required_bytes);

			CHECK_EQ(map.init(info, memory), true, "ThreadsafeMap.init succeeds with small parameters");

			CHECK_EQ(region.deinit(), true, "MemoryRegion.deinit succeeds after successful initialization");
		}

		static void success_on_large() noexcept
		{
			ThreadsafeMap2<u32, FixedSizeValue> map;

			decltype(map)::InitInfo info;
			info.thread_count = 1024;
			info.map.reserve_count = 1u << 31;
			info.map.initial_commit_count = 1u << 20;
			info.map.max_insertion_distance = 1024;
			info.store.reserve_strides = 1u << 31;
			info.store.per_thread_commit_increment_strides = 1u << 16;
			info.store.per_thread_initial_commit_strides = 1u << 16;

			const u64 required_bytes = map.required_bytes(info);

			MemoryRegion region;

			CHECK_EQ(region.init(required_bytes), true, "MemoryRegion.init succeeds");

			MemorySubregion memory = region.subregion(0, required_bytes);

			CHECK_EQ(map.init(info, memory), true, "ThreadsafeMap.init succeeds with large parameters");

			CHECK_EQ(region.deinit(), true, "MemoryRegion.deinit succeeds after successful initialization");
		}
	}

	namespace exclusive
	{
		namespace fixed_length
		{
			static void insert_single() noexcept
			{
				ThreadsafeMap2<u32, FixedSizeValue> map;

				decltype(map)::InitInfo info;
				info.thread_count = 16;
				info.map.reserve_count = 1u << 18;
				info.map.initial_commit_count = 1u << 12;
				info.map.max_insertion_distance = 1024;
				info.store.reserve_strides = 1u << 18;
				info.store.per_thread_commit_increment_strides = 1u << 12;
				info.store.per_thread_initial_commit_strides = 1u << 12;

				const u64 required_bytes = map.required_bytes(info);

				MemoryRegion region;

				CHECK_EQ(region.init(required_bytes), true, "MemoryRegion.init succeeds");

				MemorySubregion memory = region.subregion(0, required_bytes);

				CHECK_EQ(map.init(info, memory), true, "ThreadsafeMap.init succeeds");

				const u32 key = 0xFEEDBEEF;

				bool is_new1;

				const u32 index1 = map.index_from(0, key, hash_key(key), &is_new1);

				bool is_new2;

				const u32 index2 = map.index_from(0, key, hash_key(key), &is_new2);

				CHECK_EQ(map.value_from(index1)->m_key, key, "ThreadsafeMap.value_from returns the correct value");

				CHECK_EQ(index1, index2, "ThreadsafeMap.index_from called with the same key returns the same index");

				CHECK_EQ(is_new1, true, "ThreadsafeMap.value_from sets *opt_is_new to true on the first insertion of a key");

				CHECK_EQ(is_new2, false, "ThreadsafeMap.value_from sets *opt_is_new to false on the insertion of pre-existing key");

				CHECK_EQ(region.deinit(), true, "MemoryRegion.deinit succeeds after successful initialization");
			}

			static void insert_multiple() noexcept
			{
				ThreadsafeMap2<u32, FixedSizeValue> map;

				MemoryRegion region;

				init_standard_map(&map, &region);

				InsertThreadProcArgs<u32, FixedSizeValue> args;

				args.map = &map;
				args.duplicate_insertions = false;
				args.insertion_count = 200'000;

				run_on_threads_and_wait(1, insert_thread_proc<u32, FixedSizeValue>, &args);

				CHECK_EQ(region.deinit(), true, "MemoryRegion.deinit succeeds after successful initialization");
			}
		}

		namespace varying_length
		{
			static void insert_single() noexcept
			{
				ThreadsafeMap2<Range<char8>, VariableSizeValue> map;

				MemoryRegion region;

				init_standard_map(&map, &region);

				InsertThreadProcArgs<Range<char8>, VariableSizeValue> args;

				args.map = &map;
				args.duplicate_insertions = false;
				args.insertion_count = 1;

				run_on_threads_and_wait(1, insert_thread_proc<Range<char8>, VariableSizeValue>, &args);

				CHECK_EQ(region.deinit(), true, "MemoryRegion.deinit succeeds after successful initialization");
			}

			static void insert_multiple() noexcept
			{
				ThreadsafeMap2<Range<char8>, VariableSizeValue> map;

				MemoryRegion region;

				init_standard_map(&map, &region);

				InsertThreadProcArgs<Range<char8>, VariableSizeValue> args;

				args.map = &map;
				args.duplicate_insertions = false;
				args.insertion_count = 1'000;

				run_on_threads_and_wait(1, insert_thread_proc<Range<char8>, VariableSizeValue>, &args);

				CHECK_EQ(region.deinit(), true, "MemoryRegion.deinit succeeds after successful initialization");
			}
		}
	}

	namespace parallel
	{
		namespace fixed_length
		{
			static void insert_no_overlap() noexcept
			{
				ThreadsafeMap2<u32, FixedSizeValue> map;

				MemoryRegion region;

				init_standard_map(&map, &region);

				InsertThreadProcArgs<u32, FixedSizeValue> args;

				args.map = &map;
				args.duplicate_insertions = false;
				args.insertion_count = 200'000 / 16;

				run_on_threads_and_wait(16, insert_thread_proc<u32, FixedSizeValue>, &args);

				CHECK_EQ(region.deinit(), true, "MemoryRegion.deinit succeeds after successful initialization");
			}

			static void insert_overlap() noexcept
			{
				ThreadsafeMap2<u32, FixedSizeValue> map;

				MemoryRegion region;

				init_standard_map(&map, &region);

				InsertThreadProcArgs<u32, FixedSizeValue> args;

				args.map = &map;
				args.duplicate_insertions = true;
				args.insertion_count = 200'000;

				// @TODO: This sometimes fails on the assertion in ThreadsafeMap.release_thread_write_lock with old_write_lock being 0.
				// @TODO: It also sometimes fails the assert that *out_is_new is set to false on reinsertion.
				run_on_threads_and_wait(16, insert_thread_proc<u32, FixedSizeValue>, &args);

				CHECK_EQ(region.deinit(), true, "MemoryRegion.deinit succeeds after successful initialization");
			}
		}

		namespace varying_length
		{
			static void insert_no_overlap() noexcept
			{
				ThreadsafeMap2<Range<char8>, VariableSizeValue> map;

				MemoryRegion region;

				init_standard_map(&map, &region);

				InsertThreadProcArgs<Range<char8>, VariableSizeValue> args;

				args.map = &map;
				args.duplicate_insertions = false;
				args.insertion_count = 1000 / 16;

				run_on_threads_and_wait(16, insert_thread_proc<Range<char8>, VariableSizeValue>, &args);

				CHECK_EQ(region.deinit(), true, "MemoryRegion.deinit succeeds after successful initialization");
			}

			static void insert_overlap() noexcept
			{
				ThreadsafeMap2<Range<char8>, VariableSizeValue> map;

				MemoryRegion region;

				init_standard_map(&map, &region);

				InsertThreadProcArgs<Range<char8>, VariableSizeValue> args;

				args.map = &map;
				args.duplicate_insertions = true;
				args.insertion_count = 1000;

				run_on_threads_and_wait(16, insert_thread_proc<Range<char8>, VariableSizeValue>, &args);

				CHECK_EQ(region.deinit(), true, "MemoryRegion.deinit succeeds after successful initialization");
			}
		}
	}
}

static void map() noexcept
{
	using namespace map_tests;

	init::success_on_normal();

	init::success_on_small();

	init::success_on_large();

	exclusive::fixed_length::insert_single();

	exclusive::fixed_length::insert_multiple();

	exclusive::varying_length::insert_single();

	exclusive::varying_length::insert_multiple();

	parallel::fixed_length::insert_no_overlap();

	parallel::fixed_length::insert_overlap();

	parallel::varying_length::insert_no_overlap();

	parallel::varying_length::insert_overlap();
}

void test::threading() noexcept
{
	ringbuffer();

	indexstacklist();

	stridedindexstacklist();

	map();
}
