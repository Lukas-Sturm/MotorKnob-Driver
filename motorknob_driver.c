#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/i2c.h>
#include <linux/proc_fs.h>
#include <linux/kobject.h>

// Module Metadata
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Lukas Sturm");
MODULE_DESCRIPTION("Manages a Motorknob, a Motor powered Input device");
MODULE_VERSION("0.1");

// Command Structure
#define WRITE_REQUEST 0b10000000

#define DATA_START_POS   0b00000000
#define DATA_END_POS     0b00000001
#define DATA_DETENTS     0b00000010
#define DATA_CURRENT_POS 0b00000011

#define WRITE_START_POS (WRITE_REQUEST | DATA_START_POS)
#define WRITE_END_POS   (WRITE_REQUEST | DATA_END_POS)
#define WRITE_DETENTS   (WRITE_REQUEST | DATA_DETENTS)

// sysfs
// static struct proc_dir_entry *proc_file;
static struct kobject *motorknob_kobj;
static struct kobject *motorknob_profile_kobj;

// i2c
static struct i2c_client *motorknob_client;

/**
 * Writes a word (16bit) to MotorKnob
 * Uses first and second element in buffer
 */
static ssize_t motorknob_write(u8 reg, const char *user_buffer, size_t count) {
    // Check for valid data length
    if (count < 2) {
        return -EINVAL; // Invalid argument (too few bytes)
    }

    // build word
    uint16_t word = ((uint16_t) user_buffer[0]) << 8;
    word ^= user_buffer[1];

    // use smbus protocol to transfer
    s32 ret = i2c_smbus_write_word_data(motorknob_client, reg, word);
    
    if (ret < 0) {
        pr_err("Failed to send data: %d\n", ret);
        return ret;
    }

    return count; // Indicate successful write of all bytes
}

/**
 * Reads a word (16bit) from MotorKnob
 * buffer needs to be atleast two bytes big
 */
static ssize_t motorknob_read(u8 reg, char *user_buffer) {
    s32 result = i2c_smbus_read_word_data(motorknob_client, reg);

    if (result < 0) {
	    pr_err("Failed to read byte");
    	return result;
    }

    user_buffer[0] = (u8) result;
    user_buffer[1] = (u8) (result >> 8);

    return 2;
}

/**
 * Reads number of detents from Knob
 */
static ssize_t read_detents(struct kobject *kobj, struct kobj_attribute *attr, char *buffer) {
    return motorknob_read(DATA_DETENTS, buffer);
}

/**
 * Writes number of detents
 */
static ssize_t write_detents(struct kobject *kobj, struct kobj_attribute *attr, const char *buffer, size_t count) {
    return motorknob_write(WRITE_DETENTS, buffer, count);
}

/**
 * Reads start position from Knob
 */
static ssize_t read_start_position(struct kobject *kobj, struct kobj_attribute *attr, char *buffer) {
    return motorknob_read(DATA_START_POS, buffer);
}

/**
 * Writes new start position
 */
static ssize_t write_start_position(struct kobject *kobj, struct kobj_attribute *attr, const char *buffer, size_t count) {
    return motorknob_write(WRITE_START_POS, buffer, count);
}

/**
 * Reads end position from Knob
 */
static ssize_t read_end_position(struct kobject *kobj, struct kobj_attribute *attr, char *buffer) {
    return motorknob_read(DATA_END_POS, buffer);
}

/**
 * Writes new end position
 */
static ssize_t write_end_position(struct kobject *kobj, struct kobj_attribute *attr, const char *buffer, size_t count) {
    return motorknob_write(WRITE_END_POS, buffer, count);
}

/**
 * Reads position from Knob
 */
static ssize_t read_position(struct kobject *kobj, struct kobj_attribute *attr, char *buffer) {
    return motorknob_read(DATA_CURRENT_POS, buffer);
}

// sysfs files
static struct kobj_attribute detent_attr = __ATTR(detents, 0660, read_detents, write_detents);
static struct kobj_attribute start_pos_attr = __ATTR(start_position, 0660, read_start_position, write_start_position);
static struct kobj_attribute end_pos_attr = __ATTR(end_position, 0660, read_end_position, write_end_position);
static struct kobj_attribute position_attr = __ATTR(position, 0440, read_position, NULL); // only read

/**
 * Creates sysfs entries including error handling
 */
int setup_sysfs(void) {
    // there is probably a cooler way to set this up
    // error handling gets quite cumbersome

    motorknob_kobj = kobject_create_and_add("motorknob", NULL);
    if(!motorknob_kobj) {
	    printk("motorknob-sysfs - Error creating /sys/motorknob\n");
	    return -ENOMEM;
    }
    
    motorknob_profile_kobj = kobject_create_and_add("profile", motorknob_kobj);
    if(!motorknob_profile_kobj) {
	    printk("motorknob-sysfs - Error creating /sys/motorknob/profile\n");
	    kobject_put(motorknob_kobj);
	    return -ENOMEM;
    }

    if(sysfs_create_file(motorknob_profile_kobj, &detent_attr.attr)) {
	    printk("motorknob-sysfs - Error creating /sys/motorknob/profile/detents\n");
	    kobject_put(motorknob_kobj);
	    kobject_put(motorknob_profile_kobj);
	    return -ENOMEM;
    }
    
    if(sysfs_create_file(motorknob_profile_kobj, &start_pos_attr.attr)) {
	    printk("motorknob-sysfs - Error creating /sys/motorknob/profile/start_position\n");
	    sysfs_remove_file(motorknob_profile_kobj, &detent_attr.attr);
	    kobject_put(motorknob_profile_kobj);
	    kobject_put(motorknob_kobj);
	    return -ENOMEM;
    }
    
    if(sysfs_create_file(motorknob_profile_kobj, &end_pos_attr.attr)) {
	    printk("motorknob-sysfs - Error creating /sys/motorknob/profile/end_position\n");
	    sysfs_remove_file(motorknob_profile_kobj, &detent_attr.attr);
	    sysfs_remove_file(motorknob_profile_kobj, &start_pos_attr.attr);
	    kobject_put(motorknob_profile_kobj);
	    kobject_put(motorknob_kobj);
	    return -ENOMEM;
    }
    
    if(sysfs_create_file(motorknob_kobj, &position_attr.attr)) {
	    printk("motorknob-sysfs - Error creating /sys/motorknob/position\n");
	    sysfs_remove_file(motorknob_profile_kobj, &detent_attr.attr);
	    sysfs_remove_file(motorknob_profile_kobj, &start_pos_attr.attr);
	    sysfs_remove_file(motorknob_profile_kobj, &end_pos_attr.attr);
	    kobject_put(motorknob_profile_kobj);
	    kobject_put(motorknob_kobj);
	    return -ENOMEM;
    }

	printk("motorknob-sysfs - Created /sys/motorknob/*\n");

    return 0;
}

/**
 * Removes all sysfs entires
 */
void destory_sysfs(void) {
    printk("motorknob-sysfs - Deleting entries\n");
    sysfs_remove_file(motorknob_profile_kobj, &detent_attr.attr);
    sysfs_remove_file(motorknob_profile_kobj, &start_pos_attr.attr);
    sysfs_remove_file(motorknob_profile_kobj, &end_pos_attr.attr);
    sysfs_remove_file(motorknob_kobj, &position_attr.attr);
    
    kobject_put(motorknob_profile_kobj);
    kobject_put(motorknob_kobj);
}

// replaced by sysfs
// static struct proc_ops fops = {
// 	.proc_write = motorknob_write,
// 	.proc_read = motorknob_read,
// };

/**
 * Gets called when a new device gets attached to this driver
 * Just accepts it as the MotorKnob
 */
static int my_i2c_probe(struct i2c_client *client) {
    motorknob_client = client;

    // Perform any necessary client-specific initialization here
    // (e.g., configure registers, allocate resources)

    dev_info(&client->dev, "I2C Motorknob client probed\n");

    int sysfs_setup_result = setup_sysfs();
    if (sysfs_setup_result < 0) {
        return sysfs_setup_result;
    }

    // Better not use this. it is easy to use but not the right place
    // use sysfs instead
    // proc_file = proc_create("motorknob", 0666, NULL, &fops);
    // if(proc_file == NULL) {
    //	dev_err(&client->dev, "Error creating /proc/motorknob\n");
    //	return -ENOMEM;
    //}

    return 0;
}

/**
 * Gets called when a device is removed
 */
static void my_i2c_remove(struct i2c_client *client) {
    dev_info(&client->dev, "I2C Motorknob client removed\n");
    
    destory_sysfs();
    //proc_remove(proc_file);
}

// associates devices name motorknob with this driver
// echo motorknob 0x55 > /sys/bus/i2c/devices/i2c-1/new_device
static const struct i2c_device_id my_i2c_id_table[] = {
    { "motorknob", },
    { }
};
MODULE_DEVICE_TABLE(i2c, my_i2c_id_table);

// basic i2c driver
static struct i2c_driver motorknob_i2c_driver = {
    .driver = {
        .name = "motorknob-i2c-driver",
    },
    .id_table = my_i2c_id_table,
    .probe = my_i2c_probe,
    .remove = my_i2c_remove,
};
module_i2c_driver(motorknob_i2c_driver);
