#include <string.h>
#include <hardware/pio.h>
#include "config.h"
#include "abus.h"
#include "abus.pio.h"
#include "buffers.h"


#if CONFIG_PIN_APPLEBUS_PHI0 != PHI0_GPIO
#error CONFIG_PIN_APPLEBUS_PHI0 and PHI0_GPIO must be set to the same pin
#endif


enum {
    ABUS_MAIN_SM = 0,
    ABUS_DEVICE_READ_SM = 1,
};

typedef void (*shadow_handler)(bool is_write, uint_fast16_t address, uint_fast8_t data);


static shadow_handler softsw_handlers[128];


static void abus_device_read_setup(PIO pio, uint sm) {
    uint program_offset = pio_add_program(pio, &abus_device_read_program);
    pio_sm_claim(pio, sm);

    pio_sm_config c = abus_device_read_program_get_default_config(program_offset);

    // set the "device selected" pin as the jump pin
    sm_config_set_jmp_pin(&c, CONFIG_PIN_APPLEBUS_DEVSEL);

    // map the OUT pin group to the data signals
    sm_config_set_out_pins(&c, CONFIG_PIN_APPLEBUS_DATA_BASE, 8);

    // map the SET pin group to the Data transceiver control signals
    sm_config_set_set_pins(&c, CONFIG_PIN_APPLEBUS_CONTROL_BASE, 2);

    pio_sm_init(pio, sm, program_offset, &c);

    // All the GPIOs are shared and setup by the main program
}


static void abus_main_setup(PIO pio, uint sm) {
    uint program_offset = pio_add_program(pio, &abus_program);
    pio_sm_claim(pio, sm);

    pio_sm_config c = abus_program_get_default_config(program_offset);

    // set the bus R/W pin as the jump pin
    sm_config_set_jmp_pin(&c, CONFIG_PIN_APPLEBUS_RW);

    // map the IN pin group to the data signals
    sm_config_set_in_pins(&c, CONFIG_PIN_APPLEBUS_DATA_BASE);

    // map the SET pin group to the bus transceiver enable signals
    sm_config_set_set_pins(&c, CONFIG_PIN_APPLEBUS_CONTROL_BASE + 1, 3);

    // configure left shift into ISR & autopush every 26 bits
    sm_config_set_in_shift(&c, false, true, 26);

    pio_sm_init(pio, sm, program_offset, &c);

    // configure the GPIOs
    // Ensure all transceivers will start disabled, with Data transceiver direction set to 'in'
    pio_sm_set_pins_with_mask(
        pio, sm, (uint32_t)0xe << CONFIG_PIN_APPLEBUS_CONTROL_BASE, (uint32_t)0xf << CONFIG_PIN_APPLEBUS_CONTROL_BASE);
    pio_sm_set_pindirs_with_mask(pio, sm, (0xf << CONFIG_PIN_APPLEBUS_CONTROL_BASE),
        (1 << CONFIG_PIN_APPLEBUS_PHI0) | (0xf << CONFIG_PIN_APPLEBUS_CONTROL_BASE) | (0x3ff << CONFIG_PIN_APPLEBUS_DATA_BASE));

    // Disable input synchronization on input pins that are sampled at known stable times
    // to shave off two clock cycles of input latency
    pio->input_sync_bypass |= (0x3ff << CONFIG_PIN_APPLEBUS_DATA_BASE);

    pio_gpio_init(pio, CONFIG_PIN_APPLEBUS_PHI0);
    gpio_set_pulls(CONFIG_PIN_APPLEBUS_PHI0, false, false);

    for(int pin = CONFIG_PIN_APPLEBUS_CONTROL_BASE; pin < CONFIG_PIN_APPLEBUS_CONTROL_BASE + 4; pin++) {
        pio_gpio_init(pio, pin);
    }

    for(int pin = CONFIG_PIN_APPLEBUS_DATA_BASE; pin < CONFIG_PIN_APPLEBUS_DATA_BASE + 10; pin++) {
        pio_gpio_init(pio, pin);
        gpio_set_pulls(pin, false, false);
    }
}


static void __time_critical_func(shadow_softsw_00)(bool is_write, uint_fast16_t address, uint_fast8_t data) {
    if(is_write)
        soft_80store = false;
}

static void __time_critical_func(shadow_softsw_01)(bool is_write, uint_fast16_t address, uint_fast8_t data) {
    if(is_write)
        soft_80store = true;
}

static void __time_critical_func(shadow_softsw_04)(bool is_write, uint_fast16_t address, uint_fast8_t data) {
    if(is_write)
        soft_ramwrt = false;
}

static void __time_critical_func(shadow_softsw_05)(bool is_write, uint_fast16_t address, uint_fast8_t data) {
    if(is_write)
        soft_ramwrt = true;
}

static void __time_critical_func(shadow_softsw_0c)(bool is_write, uint_fast16_t address, uint_fast8_t data) {
    if(is_write)
        soft_80col = false;
}

static void __time_critical_func(shadow_softsw_0d)(bool is_write, uint_fast16_t address, uint_fast8_t data) {
    if(is_write)
        soft_80col = true;
}

static void __time_critical_func(shadow_softsw_0e)(bool is_write, uint_fast16_t address, uint_fast8_t data) {
    if(is_write)
        soft_altcharset = false;
}

static void __time_critical_func(shadow_softsw_0f)(bool is_write, uint_fast16_t address, uint_fast8_t data) {
    if(is_write)
        soft_altcharset = true;
}

static void __time_critical_func(shadow_softsw_21)(bool is_write, uint_fast16_t address, uint_fast8_t data) {
    if(is_write) {
        if(data & 0x80) {
            soft_monochrom = true;
        } else {
            soft_monochrom = false;
        }
    }
}

static void __time_critical_func(shadow_softsw_50)(bool is_write, uint_fast16_t address, uint_fast8_t data) {
    soft_switches &= ~((uint32_t)SOFTSW_TEXT_MODE);
}

static void __time_critical_func(shadow_softsw_51)(bool is_write, uint_fast16_t address, uint_fast8_t data) {
    soft_switches |= SOFTSW_TEXT_MODE;
}

static void __time_critical_func(shadow_softsw_52)(bool is_write, uint_fast16_t address, uint_fast8_t data) {
    soft_switches &= ~((uint32_t)SOFTSW_MIX_MODE);
}

static void __time_critical_func(shadow_softsw_53)(bool is_write, uint_fast16_t address, uint_fast8_t data) {
    soft_switches |= SOFTSW_MIX_MODE;
}

static void __time_critical_func(shadow_softsw_54)(bool is_write, uint_fast16_t address, uint_fast8_t data) {
    soft_switches &= ~((uint32_t)SOFTSW_PAGE_2);
}

static void __time_critical_func(shadow_softsw_55)(bool is_write, uint_fast16_t address, uint_fast8_t data) {
    soft_switches |= SOFTSW_PAGE_2;
}

static void __time_critical_func(shadow_softsw_56)(bool is_write, uint_fast16_t address, uint_fast8_t data) {
    soft_switches &= ~((uint32_t)SOFTSW_HIRES_MODE);
}

static void __time_critical_func(shadow_softsw_57)(bool is_write, uint_fast16_t address, uint_fast8_t data) {
    soft_switches |= SOFTSW_HIRES_MODE;
}

static void __time_critical_func(shadow_softsw_5e)(bool is_write, uint_fast16_t address, uint_fast8_t data) {
    soft_dhires = true;
}

static void __time_critical_func(shadow_softsw_5f)(bool is_write, uint_fast16_t address, uint_fast8_t data) {
    if(soft_dhires) {
        // This is the VIDEO7 Magic (Not documented by apple but by a patent US4631692)
        // Apple II has softswitches and also a special 2bit shift register (two flipflops basically)
        // controlled with Softwitch 80COL and AN3. AN3 is the Clock so when AN3 goes from clear to set
        // it shifts the content of 80COL into the 2 switches.
        // this is VIDEO7 Mode
        soft_video7_mode = ((soft_video7_mode & 0x01) << 1) | (soft_80col ? 0 : 1);
    }

    soft_dhires = false;
}


void abus_init() {
    // Init states
    soft_switches = SOFTSW_TEXT_MODE;

    // Setup soft-switch handlers for the Apple model
    softsw_handlers[0x21] = shadow_softsw_21;
    softsw_handlers[0x50] = shadow_softsw_50;
    softsw_handlers[0x51] = shadow_softsw_51;
    softsw_handlers[0x52] = shadow_softsw_52;
    softsw_handlers[0x53] = shadow_softsw_53;
    softsw_handlers[0x54] = shadow_softsw_54;
    softsw_handlers[0x55] = shadow_softsw_55;
    softsw_handlers[0x56] = shadow_softsw_56;
    softsw_handlers[0x57] = shadow_softsw_57;
#ifdef APPLE_MODEL_IIE
    softsw_handlers[0x00] = shadow_softsw_00;
    softsw_handlers[0x01] = shadow_softsw_01;
    softsw_handlers[0x04] = shadow_softsw_04;
    softsw_handlers[0x05] = shadow_softsw_05;
    softsw_handlers[0x0c] = shadow_softsw_0c;
    softsw_handlers[0x0d] = shadow_softsw_0d;
    softsw_handlers[0x0e] = shadow_softsw_0e;
    softsw_handlers[0x0f] = shadow_softsw_0f;
    softsw_handlers[0x5e] = shadow_softsw_5e;
    softsw_handlers[0x5f] = shadow_softsw_5f;
#endif

    abus_device_read_setup(CONFIG_ABUS_PIO, ABUS_DEVICE_READ_SM);
    abus_main_setup(CONFIG_ABUS_PIO, ABUS_MAIN_SM);

    pio_enable_sm_mask_in_sync(CONFIG_ABUS_PIO, (1 << ABUS_MAIN_SM) | (1 << ABUS_DEVICE_READ_SM));
}


static void __time_critical_func(shadow_memory)(bool is_write, uint_fast16_t address, uint32_t value) {
    // Shadow parts of the Apple's memory by observing the bus write cycles
    static bool reset_phase_1_happening = false;

    // Mirror Video Memory from MAIN & AUX banks
    if(address < 0xc000) {
        if(!is_write)
            return;

        // Refer to "Inside the Apple IIe" p.295 for how Aux memory addressing is done
        if((address >= 0x400) && (address < 0x800)) {
            // text page 1
            uint8_t *bank;
            if(soft_80store) {
                // 80STORE takes precedence - bank is then controlled by the PAGE2 switch
                bank = (soft_switches & SOFTSW_PAGE_2) ? aux_memory : main_memory;
            } else if(soft_ramwrt) {
                bank = aux_memory;
            } else {
                bank = main_memory;
            }
            bank[address] = value & 0xff;
        } else if((address >= 0x800) && (address < 0xc00)) {
            // text page 2
            uint8_t *bank = soft_ramwrt ? aux_memory : main_memory;
            bank[address] = value & 0xff;
        } else if((address >= 0x2000) && (address < 0x4000)) {
            // hires page 1
            uint8_t *bank;
            if(soft_80store && (soft_switches & SOFTSW_HIRES_MODE)) {
                // 80STORE takes precedence - bank is then controlled by the PAGE2 switch
                bank = (soft_switches & SOFTSW_PAGE_2) ? aux_memory : main_memory;
            } else if(soft_ramwrt) {
                bank = aux_memory;
            } else {
                bank = main_memory;
            }
            bank[address] = value & 0xff;
        } else if((address >= 0x4000) && (address < 0x6000)) {
            // hires page 2
            uint8_t *bank = soft_ramwrt ? aux_memory : main_memory;
            bank[address] = value & 0xff;
        }

        return;
    }

    // Handling shadowing of the soft switches in the range 0xc000 - 0xc07f
    if(address < 0xc080) {
        shadow_handler h = softsw_handlers[address & 0x7f];
        if(h) {
            h(is_write, address, value & 0xff);
        }
        return;
    }

    // Reset detection
    if((address == 0xfffc) && !is_write) {
        reset_phase_1_happening = true;
    } else if((address == 0xfffd) && !is_write && reset_phase_1_happening) {
        // Reset soft-switches
        soft_switches = SOFTSW_TEXT_MODE;
        soft_video7_mode = VIDEO7_MODE_140x192;
        soft_dhires = false;
        soft_80col = false;
        soft_80store = false;
        soft_altcharset = false;
        soft_ramwrt = false;

        reset_phase_1_happening = false;
    } else {
        reset_phase_1_happening = false;
    }
}


void __time_critical_func(abus_loop)() {
    while(1) {
        uint32_t value = pio_sm_get_blocking(CONFIG_ABUS_PIO, ABUS_MAIN_SM);

        uint_fast16_t address = (value >> 10) & 0xffff;
        bool is_write = ((value & (1u << (CONFIG_PIN_APPLEBUS_RW - CONFIG_PIN_APPLEBUS_DATA_BASE))) == 0);

        if(((value & (1u << (CONFIG_PIN_APPLEBUS_DEVSEL - CONFIG_PIN_APPLEBUS_DATA_BASE))) == 0)) {
            if(!is_write) {
                // device read access
                pio_sm_put_blocking(CONFIG_ABUS_PIO, ABUS_DEVICE_READ_SM, address & 0xf);
            }
            gpio_xor_mask(1u << PICO_DEFAULT_LED_PIN);
        }

        shadow_memory(is_write, address, value);
    }
}
