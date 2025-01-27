#include <furi.h>
#include <gui/gui.h>
#include <input/input.h>
#include <furi_hal_gpio.h>
#include <furi_hal_timer.h>

typedef struct {
    int current_position;  // 0=1000us, 1=1500us, 2=2000us
    char* position_labels[3];
    bool running;
} ServoState;

// Timer configuration for 50Hz (20ms period)
#define TIMER_PERIOD 20000

// Draw callback for the GUI
static void servo_draw_callback(Canvas* canvas, void* ctx) {
    ServoState* state = ctx;
    canvas_clear(canvas);
    
    // Draw title
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 12, "Servo Control - A7");
    
    // Draw current position
    canvas_set_font(canvas, FontSecondary);
    char position_str[32];
    snprintf(position_str, sizeof(position_str), "Position: %s", 
             state->position_labels[state->current_position]);
    canvas_draw_str(canvas, 5, 30, position_str);
    
    // Draw controls
    canvas_draw_str(canvas, 2, 70, "LEFT/RIGHT: Change position");
    canvas_draw_str(canvas, 2, 82, "OK: Toggle output");
    canvas_draw_str(canvas, 2, 94, "BACK: Exit");
    
    // Draw status
    canvas_draw_str(canvas, 2, 110, state->running ? "Status: Running" : "Status: Stopped");
}

// Input callback
static void servo_input_callback(InputEvent* input_event, void* ctx) {
    furi_assert(ctx);
    FuriMessageQueue* event_queue = ctx;
    furi_message_queue_put(event_queue, input_event, FuriWaitForever);
}

// Timer interrupt handler
static void timer_irq_handler(void* ctx) {
    ServoState* state = ctx;
    static uint32_t counter = 0;
    uint32_t pulse_width;
    
    if(state->running) {
        if(counter == 0) {
            // Start pulse
            furi_hal_gpio_write(&gpio_ext_pa7, true);
            
            // Set pulse width based on position
            switch(state->current_position) {
                case 0: pulse_width = 1000; break;  // 1000us
                case 1: pulse_width = 1500; break;  // 1500us
                case 2: pulse_width = 2000; break;  // 2000us
                default: pulse_width = 1500; break;
            }
        } else if(counter == pulse_width) {
            // End pulse
            furi_hal_gpio_write(&gpio_ext_pa7, false);
        }
    }
    
    counter++;
    if(counter >= TIMER_PERIOD) counter = 0;  // Reset every 20ms
}

// Main application entry point
int32_t servo_control_app(void* p) {
    UNUSED(p);
    
    // Initialize state
    ServoState* state = malloc(sizeof(ServoState));
    state->current_position = 1;  // Start at center position
    state->running = false;
    state->position_labels[0] = "1000us (Left)";
    state->position_labels[1] = "1500us (Center)";
    state->position_labels[2] = "2000us (Right)";
    
    // Configure GPIO pin
    furi_hal_gpio_init_simple(&gpio_ext_pa7, GpioModeOutputPushPull);
    furi_hal_gpio_write(&gpio_ext_pa7, false);
    
    // Set up GUI
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, servo_draw_callback, state);
    
    // Set up input handling
    FuriMessageQueue* event_queue = furi_message_queue_alloc(8, sizeof(InputEvent));
    view_port_input_callback_set(view_port, servo_input_callback, event_queue);
    
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);
    
    // Configure timer (TIM2) for 1MHz (1us resolution)
    furi_hal_bus_enable(FuriHalBusTIM2);
    LL_TIM_InitTypeDef tim_init = {
        .Prescaler = 64-1,  // 64MHz/64 = 1MHz
        .CounterMode = LL_TIM_COUNTERMODE_UP,
        .Autoreload = TIMER_PERIOD-1,
    };
    LL_TIM_Init(TIM2, &tim_init);
    LL_TIM_EnableIT_UPDATE(TIM2);
    LL_TIM_EnableCounter(TIM2);
    
    // Set up timer interrupt
    furi_hal_interrupt_set_isr(FuriHalInterruptIdTIM2, timer_irq_handler, state);
    
    InputEvent event;
    bool running = true;
    
    // Main loop
    while(running) {
        if(furi_message_queue_get(event_queue, &event, 100) == FuriStatusOk) {
            if(event.type == InputTypePress) {
                switch(event.key) {
                    case InputKeyLeft:
                        if(state->current_position > 0) state->current_position--;
                        break;
                    case InputKeyRight:
                        if(state->current_position < 2) state->current_position++;
                        break;
                    case InputKeyOk:
                        state->running = !state->running;
                        break;
                    case InputKeyBack:
                        running = false;
                        break;
                    default:
                        break;
                }
            }
        }
        view_port_update(view_port);
    }
    
    // Cleanup
    state->running = false;
    furi_hal_gpio_write(&gpio_ext_pa7, false);
    
    LL_TIM_DisableCounter(TIM2);
    LL_TIM_DisableIT_UPDATE(TIM2);
    furi_hal_interrupt_set_isr(FuriHalInterruptIdTIM2, NULL, NULL);
    furi_hal_bus_disable(FuriHalBusTIM2);
    
    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_message_queue_free(event_queue);
    furi_record_close(RECORD_GUI);
    free(state);
    
    return 0;
}
