#include <linux/module.h>
#define INCLUDE_VERMAGIC
#include <linux/build-salt.h>
#include <linux/elfnote-lto.h>
#include <linux/export-internal.h>
#include <linux/vermagic.h>
#include <linux/compiler.h>

#ifdef CONFIG_UNWINDER_ORC
#include <asm/orc_header.h>
ORC_HEADER;
#endif

BUILD_SALT;
BUILD_LTO_INFO;

MODULE_INFO(vermagic, VERMAGIC_STRING);
MODULE_INFO(name, KBUILD_MODNAME);

__visible struct module __this_module
__section(".gnu.linkonce.this_module") = {
	.name = KBUILD_MODNAME,
	.init = init_module,
#ifdef CONFIG_MODULE_UNLOAD
	.exit = cleanup_module,
#endif
	.arch = MODULE_ARCH_INIT,
};

#ifdef CONFIG_MITIGATION_RETPOLINE
MODULE_INFO(retpoline, "Y");
#endif



static const struct modversion_info ____versions[]
__used __section("__versions") = {
	{ 0x83da000e, "param_ops_ullong" },
	{ 0x7f3b62fe, "drm_open" },
	{ 0xc1514a3b, "free_irq" },
	{ 0xc80ab559, "swake_up_one" },
	{ 0xa78af5f3, "ioread32" },
	{ 0xdc880cef, "drm_poll" },
	{ 0x5a6efd55, "param_ops_uint" },
	{ 0x67620c6, "pci_enable_device" },
	{ 0x4a453f53, "iowrite32" },
	{ 0x7f02188f, "__msecs_to_jiffies" },
	{ 0x94a18028, "pci_iomap" },
	{ 0x920e8334, "pci_alloc_irq_vectors" },
	{ 0x656e4a6e, "snprintf" },
	{ 0xc5b6f236, "queue_work_on" },
	{ 0x9c11e3fb, "drm_mode_probed_add" },
	{ 0x69b825f7, "drm_gem_shmem_dumb_create" },
	{ 0xc8c85086, "sg_free_table" },
	{ 0x48d88a2c, "__SCT__preempt_schedule" },
	{ 0x608741b5, "__init_swait_queue_head" },
	{ 0x92540fbf, "finish_wait" },
	{ 0x5f30451, "drm_atomic_helper_shutdown" },
	{ 0x355f4c49, "__pci_register_driver" },
	{ 0x63659537, "drm_gem_simple_kms_begin_shadow_fb_access" },
	{ 0xb52801ce, "pci_disable_msi" },
	{ 0x6df1aaf1, "kernel_sigaction" },
	{ 0xd4defbf9, "pci_request_regions" },
	{ 0x48d7031e, "drm_mode_object_get" },
	{ 0x69acdf38, "memcpy" },
	{ 0x37a0cba, "kfree" },
	{ 0x7ab1f0cb, "pcpu_hot" },
	{ 0xc3055d20, "usleep_range_state" },
	{ 0x8c26d495, "prepare_to_wait_event" },
	{ 0xb3f7646e, "kthread_should_stop" },
	{ 0xe2964344, "__wake_up" },
	{ 0x10ed95a8, "pci_irq_vector" },
	{ 0x2e86bdcd, "drmm_mode_config_init" },
	{ 0x34db050b, "_raw_spin_lock_irqsave" },
	{ 0xb19a5453, "__per_cpu_offset" },
	{ 0xba8fbd64, "_raw_spin_lock" },
	{ 0xe0872c22, "pci_unregister_driver" },
	{ 0xbdfb6dbb, "__fentry__" },
	{ 0xcfabba12, "wake_up_process" },
	{ 0xef6e2dc7, "drm_mode_object_put" },
	{ 0x65487097, "__x86_indirect_thunk_rax" },
	{ 0x122c3a7e, "_printk" },
	{ 0x8427cc7b, "_raw_spin_lock_irq" },
	{ 0xab3afca4, "prepare_to_swait_event" },
	{ 0x1d24c881, "___ratelimit" },
	{ 0x1000e51, "schedule" },
	{ 0x8ddd8aad, "schedule_timeout" },
	{ 0xa1875288, "__drmm_add_action_or_reset" },
	{ 0x9cb986f2, "vmalloc_base" },
	{ 0xf0fdf6cb, "__stack_chk_fail" },
	{ 0xb2fcb56d, "queue_delayed_work_on" },
	{ 0xa95d79a7, "drm_atomic_helper_commit" },
	{ 0xd9e2d8a5, "drm_atomic_helper_check" },
	{ 0xb5740d4c, "drm_atomic_helper_connector_destroy_state" },
	{ 0x618911fc, "numa_node" },
	{ 0xa5b7a741, "drm_gem_mmap" },
	{ 0x4985390f, "_dev_info" },
	{ 0x3870f1bb, "drm_ioctl" },
	{ 0x87a21cb3, "__ubsan_handle_out_of_bounds" },
	{ 0x7cd8d75e, "page_offset_base" },
	{ 0xb3f985a8, "sg_alloc_table" },
	{ 0x4323050a, "pci_find_capability" },
	{ 0xa2f53452, "drm_gem_simple_kms_destroy_shadow_plane_state" },
	{ 0xfe487975, "init_wait_entry" },
	{ 0x43f8abd5, "drm_dev_unplug" },
	{ 0xd554b05f, "drm_connector_init" },
	{ 0x92bed554, "pci_enable_msi" },
	{ 0xceb19f3e, "_dev_err" },
	{ 0x6091797f, "synchronize_rcu" },
	{ 0x92d5838e, "request_threaded_irq" },
	{ 0x15279fef, "drm_atomic_helper_damage_merged" },
	{ 0xf79ec7c7, "noop_llseek" },
	{ 0xba082f59, "drm_read" },
	{ 0xf847c112, "drm_gem_fb_create_with_dirty" },
	{ 0x4c03a563, "random_kmalloc_seed" },
	{ 0x5b1ec2db, "finish_swait" },
	{ 0x4dfa8d4b, "mutex_lock" },
	{ 0x7475b515, "dma_alloc_attrs" },
	{ 0x2e5f3dce, "pci_read_config_word" },
	{ 0x4b750f53, "_raw_spin_unlock_irq" },
	{ 0x4c9d28b0, "phys_base" },
	{ 0x53a1e8d9, "_find_next_bit" },
	{ 0x5a5a2271, "__cpu_online_mask" },
	{ 0x1126c0d7, "drm_mode_duplicate" },
	{ 0x689f3974, "kthread_stop" },
	{ 0xcefb0c9f, "__mutex_init" },
	{ 0x4a35d30d, "drm_mode_set_name" },
	{ 0xd35cce70, "_raw_spin_unlock_irqrestore" },
	{ 0x71cf81bb, "pci_iounmap" },
	{ 0x3dad9978, "cancel_delayed_work" },
	{ 0x617931ae, "__devm_drm_dev_alloc" },
	{ 0xe82eafbd, "drm_gem_shmem_prime_import_sg_table" },
	{ 0xfb578fc5, "memset" },
	{ 0x36d2243, "_dev_warn" },
	{ 0xd0082417, "drmm_kmalloc" },
	{ 0x6794def6, "pci_set_master" },
	{ 0x5b8239ca, "__x86_return_thunk" },
	{ 0x17de3d5, "nr_cpu_ids" },
	{ 0x5a8491fb, "drm_fbdev_generic_setup" },
	{ 0xd9a5ea54, "__init_waitqueue_head" },
	{ 0x30f43223, "kthread_bind" },
	{ 0xd7a8abef, "drm_atomic_helper_connector_duplicate_state" },
	{ 0xc465ef01, "drm_simple_display_pipe_init" },
	{ 0x8e0a6121, "pcie_capability_clear_and_set_word_unlocked" },
	{ 0x2881d58a, "drm_connector_cleanup" },
	{ 0x15ba50a6, "jiffies" },
	{ 0x3817aecb, "kthread_create_on_node" },
	{ 0x42b507fc, "dma_set_coherent_mask" },
	{ 0x3c3ff9fd, "sprintf" },
	{ 0x97651e6c, "vmemmap_base" },
	{ 0xa648e561, "__ubsan_handle_shift_out_of_bounds" },
	{ 0xd53821fa, "drm_object_property_set_value" },
	{ 0xa9962270, "dma_free_attrs" },
	{ 0x64b0718b, "drm_gem_simple_kms_duplicate_shadow_plane_state" },
	{ 0x999e8297, "vfree" },
	{ 0x3213f038, "mutex_unlock" },
	{ 0x9fa7184a, "cancel_delayed_work_sync" },
	{ 0xc6f46339, "init_timer_key" },
	{ 0x2e1c1b75, "param_ops_bool" },
	{ 0x8bf35ae8, "pci_release_regions" },
	{ 0xeae3dfd6, "__const_udelay" },
	{ 0xfbe215e4, "sg_next" },
	{ 0x6729d3df, "__get_user_4" },
	{ 0x2cf56265, "__dynamic_pr_debug" },
	{ 0x3c12dfe, "cancel_work_sync" },
	{ 0xc6f1893c, "drm_helper_probe_single_connector_modes" },
	{ 0xffeedf6a, "delayed_work_timer_fn" },
	{ 0x90bf6b90, "pci_disable_msix" },
	{ 0x48597ccd, "drm_gem_simple_kms_reset_shadow_plane" },
	{ 0x2e4b8e3f, "pci_disable_device" },
	{ 0xe16cab4a, "boot_cpu_data" },
	{ 0x109cea40, "pcie_set_readrq" },
	{ 0x935c45ad, "dma_set_mask" },
	{ 0x17c12aa6, "drm_atomic_helper_connector_reset" },
	{ 0xed85405, "drm_mode_config_reset" },
	{ 0x419789e6, "dma_unmap_sg_attrs" },
	{ 0xbf55f104, "kmalloc_trace" },
	{ 0xfd819835, "pci_read_config_byte" },
	{ 0x54b1fac6, "__ubsan_handle_load_invalid_value" },
	{ 0xd6ee688f, "vmalloc" },
	{ 0x1c7c3948, "pci_write_config_word" },
	{ 0xb5b54b34, "_raw_spin_unlock" },
	{ 0x41dc78bd, "drm_gem_simple_kms_end_shadow_fb_access" },
	{ 0x62e96129, "drm_compat_ioctl" },
	{ 0x902df0e1, "drm_dev_register" },
	{ 0xeb233a45, "__kmalloc" },
	{ 0xe2c17b5d, "__SCT__might_resched" },
	{ 0x1004e946, "kmalloc_caches" },
	{ 0xb5dcde8d, "drm_release" },
	{ 0x6fa56088, "dma_map_sg_attrs" },
	{ 0x2d3385d3, "system_wq" },
	{ 0x2f2c95c4, "flush_work" },
	{ 0x73776b79, "module_layout" },
};

MODULE_INFO(depends, "");

MODULE_ALIAS("pci:v000010EEd00009048sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00009044sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00009042sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00009041sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd0000903Fsv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00009038sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00009028sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00009018sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00009034sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00009024sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00009014sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00009032sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00009022sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00009012sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00009031sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00009021sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00009011sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00008011sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00008012sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00008014sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00008018sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00008021sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00008022sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00008024sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00008028sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00008031sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00008032sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00008034sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00008038sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00007011sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00007012sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00007014sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00007018sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00007021sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00007022sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00007024sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00007028sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00007031sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00007032sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00007034sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00007038sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00006828sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00006830sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00006928sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00006930sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00006A28sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00006A30sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00006D30sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00004808sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00004828sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00004908sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00004A28sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00004B28sv*sd*bc*sc*i*");
MODULE_ALIAS("pci:v000010EEd00002808sv*sd*bc*sc*i*");

MODULE_INFO(srcversion, "AC301E6F44E6EF4A6C941C7");
