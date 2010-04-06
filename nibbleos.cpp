
#include <WProgram.h>

//// NibbleOS

// enter opcode value using 4 buttons, possibly 0000, possibly 1111
// press action
// the opcode will have a certain number of arguments, possibly 0
// keep reading these using the same method,
// once recieved, apply them to the operation.
// repeat.

// Data sheet:
//  0: noop       -- noop;                                             does nothing.
//  1: push       -- push: Val;                                        adds value Val to top of stack.
//  2: add        -- push: 2; push: 3; add;                            removes two values off the stack, adds them and puts the result on top of stack.
//  3: define     -- push: Addr; push: Size; define: code1, code2...;  Creates a procedure at call address Addr, with Size number of words in the code.
//  4: ret        -- ret;                                              marks the end of a sequence of code.
//  5: call       -- push: Addr; call:                                 calls the procedure at the address, before continuing the current code.
//  6: stackcount -- stackcount;                                       displays the number of items on the stack (or 15 if it is more than that).
//  7: show       -- show;                                             displays value at TOS.
//  8: del        -- push: Sn; del;                                    remove the value from offset Sn below TOS, if there is one.
//  9: copy       -- push: Sn; copy;                                   copies value from offset Sn below TOS and pushes onto stack.
// 10: sub        -- push: X; push: Y; sub;                            takes X and Y off and replaces with (X - Y)
// 11: jnz        -- push: Val; push: Addr; jnz;                       jump to Addr if Val is not zero, and stop executing the current code.
// 12:
// 13:
// 14:
// 15: page2      -- push: Opcode; page2;                              allows calling a primitive opcode in 16 - 31 .

/* Some example library codez:

 1 1  push 1
 1 3  push 3
 3    define  -- +1
 1 1  push 1
 2    add

 1 2  push 2
 1 3  push 3
 3    define  -- dup
 1 0  push 0
 9    copy

-- still working on this one!
 1 3  push 3
 1 9  push 9
 3    define  -- push3'
 1 2  push 2
 5    call    -- dup
 1 4  push 4
 1 5  push 5
 5    call    -- subr
 1 1  push 1
 2    add
 1 2  push 3
 B    jnz     -- push3'

 1 4  push 4
 1 5  push 5
 3    define  -- push3  [TOS] -> [TOS 1 2 3]
 1 1  push 1
 1 3  push 3
 5    call    -- push3'

 1 5  push 5
 1 7  push 7
 3    define  -- subr   subr x y -> sub y x
 1 1  push 1
 9    copy
 A    sub
 1 1  push 1
 8    del


!!! This has a bug, it will eat up its nz value in the test.

 1 6  push 6
 1 6  push 6
 3    define  -- not
 1 7  push 7
 B    jnz     -- not'
 1 1  push 1
 2    add

 1 7  push 7
 1 4  push 4
 3    define  -- not'
 1 2  push 2
 5    call    -- dup
 A    sub

*/

#define STACK_SIZE  256

/* If the heap ever gets bigger than that then global[] 's type needs to change */
#define HEAP_SIZE   256

// It is only possible to reference 16 globals using a nibble.
// To improve this, we would need to change the call opcode to take 2 nibbles as input.
#define GLOBAL_SIZE 16

#define LED5_FLASH_ON  150
#define LED5_FLASH_OFF 150

#define CLK 300

#define BUTTON1 7
#define BUTTON2 8
#define BUTTON3 9
#define BUTTON4 10
#define BUTTON5 11

#define LED1 6
#define LED2 5
#define LED3 4
#define LED4 3
#define LED5 2

static inline void retrieve_more_op (uint8_t);

static uint8_t stack[STACK_SIZE];
static uint8_t heap[HEAP_SIZE];
static uint8_t global[GLOBAL_SIZE]; // holds pointers into heap.

static uint8_t *sp = stack;
static uint8_t *hp = heap + HEAP_SIZE; // points to top of heap

static uint8_t value1 = LOW;
static uint8_t value2 = LOW;
static uint8_t value3 = LOW;
static uint8_t value4 = LOW;
static uint8_t value5 = LOW;

// Be able to refer to more than 256 milliseconds
static uint16_t delay_taken = 0;

static uint8_t readingopcode = true;
static uint8_t buttonsum     = 0;
static uint8_t theopcode     = 0;
static uint8_t dobreak       = false;

// These limit the number of nibble arguments to 256 per function call.
static uint8_t argsneeded  = 0;
static uint8_t argswaiting = 0;

static inline void startup (void) {
  pinMode( BUTTON1, INPUT);
  pinMode( BUTTON2, INPUT);
  pinMode( BUTTON3, INPUT);
  pinMode( BUTTON4, INPUT);
  pinMode( BUTTON5, INPUT);

  pinMode( LED1, OUTPUT);
  pinMode( LED2, OUTPUT);
  pinMode( LED3, OUTPUT);
  pinMode( LED4, OUTPUT);
  pinMode( LED5, OUTPUT);
}

static inline void toggle1 (void) {
  value1 = (value1 == HIGH) ? LOW : HIGH;
}

static inline void toggle2 (void) {
  value2 = (value2 == HIGH) ? LOW : HIGH;
}

static inline void toggle3 (void) {
  value3 = (value3 == HIGH) ? LOW : HIGH;
}

static inline void toggle4 (void) {
  value4 = (value4 == HIGH) ? LOW : HIGH;
}

static inline void toggle5 (void) {
  value5 = (value5 == HIGH) ? LOW : HIGH;
}

static inline void read_value (void) {
  if (digitalRead( BUTTON1) == HIGH) {
    toggle1();
  }
  if (digitalRead( BUTTON2) == HIGH) {
    toggle2();
  }
  if (digitalRead( BUTTON3) == HIGH) {
    toggle3();
  }
  if (digitalRead( BUTTON4) == HIGH) {
    toggle4();
  }
}

static inline void button_sum (void) {
  buttonsum = 0;
  if (value1 == HIGH) {
    buttonsum += 8;
  }
  if (value2 == HIGH) {
    buttonsum += 4;
  }
  if (value3 == HIGH) {
    buttonsum += 2;
  }
  if (value4 == HIGH) {
    buttonsum += 1;
  }
}

static inline void led_show_value (void) {
  digitalWrite( LED1, value1);
  digitalWrite( LED2, value2);
  digitalWrite( LED3, value3);
  digitalWrite( LED4, value4);
}

static inline void flash5 (void) {
  delay_taken = 0;
  while (delay_taken < CLK) {
    digitalWrite( LED5, value5);
    toggle5();
    delay( LED5_FLASH_ON);
    delay_taken += LED5_FLASH_ON;
  }
}

static inline void reset_values (void) {
  value1 = LOW;
  value2 = LOW;
  value3 = LOW;
  value4 = LOW;
}

static inline void set_led_values (uint8_t v) {
  value1 = (v & 8) == 8;
  value2 = (v & 4) == 4;
  value3 = (v & 2) == 2;
  value4 = (v & 1) == 1;
}

static inline void set_tos_values (void) {
  set_led_values( sp[-1]);
}

static inline void display_led_value (uint8_t val) {
  set_led_values( val);
  led_show_value();
  delay( 2000);
}

static inline void argswaiting_count (void) {
  switch (theopcode) {
  case 1: // push
    argswaiting = 1;
    break;
  case 3: // define
    argswaiting = *--sp; // number of words in the code.
    break;
  default:
    argswaiting = 0;
  }
  argsneeded = argswaiting;
}

static void run_this_code (uint8_t globn) {
  uint8_t code = global[globn];
  digitalWrite( LED5, HIGH);
  for (;; ++code) {
    retrieve_more_op( heap[code]);
    if (dobreak) {
      dobreak = false;
      break;
    }
  }
}

// remove the args from the stack and place the rv there.
static inline void execute (void) {
  uint8_t arg1;
  uint8_t arg2;
  uint8_t i;
  uint8_t *ptr;
  switch (theopcode) {
  case 2: // add
    if ((sp - stack) > 1) {
      arg1 = *--sp;
      arg2 = sp[-1];
      sp[-1] = arg1 + arg2;
    }
    break;
  case 3: // define
    // take argsneeded words from the stack.
    // place that code in the next space on heap.
    // add a pointer from globals to the code in heap with the address on tos.
    i = 0;
    *--hp = 4; // add a RET.
    for (; i < argsneeded; ++i) {
      *--hp = *--sp;
    }
    global[*--sp] = hp - heap;
    break;
  case 5: // call
    if (stack != sp) {
      run_this_code( *--sp);
    }
    break;
  case 6: // stackcount
    arg1 = sp - stack;
    display_led_value( (arg1 > 15) ? 15 : arg1);
    break;
  case 7: // show
    display_led_value( (sp > stack) ? sp[-1] : 0);
    break;
  case 8: // del
    if (stack != sp) {
      arg1 = *--sp;
      ptr = (sp - 1) - arg1;
      while (ptr < stack) { // In case we accidentally tried to delete random memory, skip past it.
        ++ptr;
      }
      if (sp > stack) {
        --sp;
        for (; ptr < sp; ++ptr) {
          *ptr = ptr[1];
        }
      }
    }
    break;
  case 9: // copy
    if (stack != sp) {
      arg1 = *--sp;
      *sp = (((sp - 1) - arg1) < stack) ? 0 : *((sp - 1) - arg1);
      ++sp;
    }
    break;
  case 10: // sub
    if ((sp - stack) > 1) {
      arg1 = *--sp;
      arg2 = sp[-1];
      sp[-1] =  arg2 - arg1;
    }
    break;
  case 11: // jnz
    if ((sp - stack) > 1) {
      arg1 = *--sp;
      arg2 = *--sp;
      if (arg2 != 0) {
        run_this_code( arg1);
        // if the code ever finishes and gets here, we need to terminate it.
        dobreak = true;
      }
    }
    break;
  default:
    break;
  }
}

static inline void retrieve_more_op (uint8_t insum) {
  if (readingopcode) {
    theopcode = insum;
    if (theopcode == 4) {
      dobreak = true;
      return;
    }
    argswaiting_count();
    readingopcode = false;
  } else {
    *sp++ = insum;
    --argswaiting;
  }
  if (!argswaiting) {
    readingopcode = true;
    execute();
    if (dobreak) {
      return; // An op can propogate a dobreak.
    }
  }
  reset_values();
}

static inline void main_loop (void) {
  for (;;) {
    read_value();
    button_sum();

    if (digitalRead( BUTTON5) == HIGH) {
      retrieve_more_op( buttonsum);
      if (dobreak) {
        dobreak = false; // we won't allow to break out of the main loop.
      }
    }
    led_show_value();
    flash5();
  }
}

int main (void) {
  init();
  startup();

  main_loop();
  return 0;
}
