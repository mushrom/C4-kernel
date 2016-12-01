#include <sigma0/sigma0.h>
#include <miniforth/miniforth.h>

struct foo {
	int target;
	int display;
	int forth;
};

void test_thread( void *unused );
void forth_thread( void *sysinfo );
void debug_print( struct foo *info, char *asdf );

void main( void ){
	unsigned *s = (void *)0x3da7e000;
	struct foo thing;
	message_t start = (message_t){ .type = MESSAGE_TYPE_CONTINUE, };

	thing.target  = 2;
	thing.display = c4_create_thread( display_thread, s, NULL, 0 );
	s -= 1024;
	thing.forth   = c4_create_thread( forth_thread,   s, &thing, 0 );

	c4_msg_send( &start, thing.display );
	c4_msg_send( &start, thing.forth );

	server( &thing );

	// TODO: panic or dump debug info or something, server()
	//       should never return
	for ( ;; );
}

void test_thread( void *data ){
	message_t msg;
	volatile struct foo *meh = data;

	msg.type = 0xcafe;

	for (;;){
		c4_msg_recieve( &msg, 0 );
		c4_msg_send( &msg, meh->target );
	}
}

extern const char *foo;

void server( void *data ){
	message_t msg;
	struct foo *meh = data;

	while ( true ){
		c4_msg_recieve( &msg, 0 );

		char c = decode_scancode( msg.data[0] );

		if ( c && msg.data[1] == 0 ){
			if ( c ){
				message_t keycode;
				keycode.type    = 0xbabe;
				keycode.data[0] = c;

				c4_msg_send( &keycode, meh->display );
				c4_msg_send( &keycode, meh->forth );
			}
		}
	}

	for ( ;; );
}

enum {
	CODE_ESCAPE,
	CODE_TAB,
	CODE_LEFT_CONTROL,
	CODE_RIGHT_CONTROL,
	CODE_LEFT_SHIFT,
	CODE_RIGHT_SHIFT,
};

const char lowercase[] =
	{ '`', CODE_ESCAPE, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-',
	  '=', '\b', CODE_TAB, 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p',
	  '[', ']', '\n', CODE_LEFT_CONTROL, 'a', 's', 'd', 'f', 'g', 'h', 'j',
	  'k', 'l', ';', '\'', '?', CODE_LEFT_SHIFT, '?', 'z', 'x', 'c', 'v', 'b',
	  'n', 'm', ',', '.', '/', CODE_RIGHT_SHIFT, '_', '_', ' ', '_', '_', '_',
	  '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_',
	};

const char uppercase[] =
	{ '~', CODE_ESCAPE, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_',
	  '+', '\b', CODE_TAB, 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P',
	  '{', '}', '\n', CODE_LEFT_CONTROL, 'A', 'S', 'D', 'F', 'G', 'H', 'J',
	  'K', 'L', ':', '"', '?', CODE_LEFT_SHIFT, '?', 'Z', 'X', 'C', 'V', 'B',
	  'N', 'M', '<', '>', '?', CODE_RIGHT_SHIFT, '_', '_', ' ', '_', '_', '_',
	  '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_', '_',
	};

char decode_scancode( unsigned long code ){
	static bool is_uppercase = false;
	char c = is_uppercase? uppercase[code] : lowercase[code];
	char ret = '\0';

	switch ( c ){
		case CODE_LEFT_SHIFT:
		case CODE_RIGHT_SHIFT:
			is_uppercase = !is_uppercase;
			break;

		default:
			ret = c;
			break;
	}

	return ret;
}

static struct foo *forth_sysinfo;

static char *read_line( char *buf, unsigned n ){
	message_t msg;
	unsigned i = 0;

	for ( i = 0; i < n - 1; i++ ){
retry:
		c4_msg_recieve( &msg, 0 );
		char c = msg.data[0];

		if ( i && c == '\b' ){
			i--;
			goto retry;
		}

		buf[i] = c;

		if ( c == '\n' ){
			break;
		}
	}

	buf[++i] = '\0';

	return buf;
}

extern char _binary_sigma0_init_commands_fs_start[];
extern char _binary_sigma0_init_commands_fs_end[];

char minift_get_char( void ){
	static char input[80];
	static bool initialized = false;
	static char *ptr;

	if ( !initialized ){
		*_binary_sigma0_init_commands_fs_end = 0;

		for ( unsigned i = 0; i < sizeof(input); i++ ){ input[i] = 0; }
		ptr         = _binary_sigma0_init_commands_fs_start;
		initialized = true;
	}

	while ( !*ptr ){
		debug_print( forth_sysinfo, "miniforth > " );
		ptr = read_line( input, sizeof( input ));
	}

	char fug[2] = { *ptr, 0 };

	return *ptr++;
}

void minift_put_char( char c ){
	message_t msg;

	msg.type    = 0xbabe;
	msg.data[0] = c;

	c4_msg_send( &msg, forth_sysinfo->display );
}

static bool c4_minift_sendmsg( minift_vm_t *vm );
static bool c4_minift_recvmsg( minift_vm_t *vm );

static minift_archive_entry_t c4_words[] = {
	{ "sendmsg", c4_minift_sendmsg, 0 },
	{ "recvmsg", c4_minift_recvmsg, 0 },
};

void forth_thread( void *sysinfo ){
	forth_sysinfo = sysinfo;

	unsigned long data[512];
	unsigned long calls[32];
	unsigned long params[32];

	minift_vm_t foo;
	minift_archive_t arc = {
		.name    = "c4",
		.entries = c4_words,
		.size    = sizeof(c4_words) / sizeof(minift_archive_entry_t),
	};

	for ( ;; ){
		minift_stack_t data_stack = {
			.start = data,
			.ptr   = data,
			.end   = data + 512,
		};

		minift_stack_t call_stack = {
			.start = calls,
			.ptr   = calls,
			.end   = calls + 32,
		};

		minift_stack_t param_stack = {
			.start = params,
			.ptr   = params,
			.end   = params + 32,
		};

		minift_init_vm( &foo, &call_stack, &data_stack, &param_stack, NULL );
		minift_archive_add( &foo, &arc );
		minift_run( &foo );
		debug_print( sysinfo, "forth vm exited, restarting...\n" );
	}
}

void debug_print( struct foo *info, char *str ){
	message_t msg;

	for ( unsigned i = 0; str[i]; i++ ){
		msg.data[0] = str[i];
		msg.type    = 0xbabe;

		c4_msg_send( &msg, info->display );
	}
}

int c4_msg_send( message_t *buffer, unsigned to ){
	int ret = 0;

	DO_SYSCALL( SYSCALL_SEND, buffer, to, 0, 0, ret );

	return ret;
}

int c4_msg_recieve( message_t *buffer, unsigned from ){
	int ret = 0;

	DO_SYSCALL( SYSCALL_RECIEVE, buffer, from, 0, 0, ret );

	return ret;
}

int c4_create_thread( void (*entry)(void *),
                      void *stack,
                      void *data,
                      unsigned flags )
{
//int c4_create_thread( void (*entry)(void *), void *stack, void *data ){
	int ret = 0;

	DO_SYSCALL( SYSCALL_CREATE_THREAD, entry, stack, data, flags, ret );

	return ret;
}

static bool c4_minift_sendmsg( minift_vm_t *vm ){
	unsigned long target = minift_pop( vm, &vm->param_stack );
	unsigned long temp   = minift_pop( vm, &vm->param_stack );
	message_t *msg = (void *)temp;

	if ( !vm->running ){
		return false;
	}

	debug_print( forth_sysinfo, "got to sendmsg\n" );
	c4_msg_send( msg, target );

	return true;
}

static bool c4_minift_recvmsg( minift_vm_t *vm ){
	//  TODO: add 'from' argument, once that's supported
	unsigned long temp   = minift_pop( vm, &vm->param_stack );
	message_t *msg = (void *)temp;

	if ( !vm->running ){
		return false;
	}

	debug_print( forth_sysinfo, "got to recvmsg\n" );
	c4_msg_recieve( msg, 0 );

	return true;
}
