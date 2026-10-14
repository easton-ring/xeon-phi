#!/bin/sh
# Count uses of kernel APIs that changed after 4.18 / 5.9 in a driver source tree.
# A size indicator for docs/kernel-port.md section 5, not a list of exact work items
# (counts include version-guarded alternatives and comments).
#
#   mic/port/apiscan.sh <source dir>
set -eu
d=${1:?usage: apiscan.sh DIR}
scan() {  # scan LABEL ERE
    n=$(grep -rEn --include='*.[ch]' -e "$2" "$d" | wc -l)
    [ "$n" -gt 0 ] && printf '%-48s %5d\n' "$1" "$n"
    return 0
}
scan "ioremap_nocache"                        'ioremap_nocache'
scan "pci_map_*/pci_alloc_consistent/dma_mask" 'pci_map_|pci_unmap_|pci_alloc_consistent|pci_set_dma_mask'
scan "pci_enable_msix"                        'pci_enable_msix\b'
scan "pci_enable_msi"                         'pci_enable_msi\b'
scan "pci_alloc_irq_vectors"                  'pci_alloc_irq_vectors'
scan "dma_map_ops"                            'dma_map_ops'
scan "get_user_pages*"                        'get_user_pages[a-z_]*\('
scan "pin_user_pages*"                        'pin_user_pages'
scan "mmap_sem"                               'mmap_sem'
scan "vm_flags writes"                        'vm_flags[[:space:]]*[|&]?='
scan "remap_pfn_range/vm_insert_page"         'vm_insert_page|remap_pfn_range|io_remap_pfn_range'
scan "class_create("                          'class_create\('
scan "create_proc_entry"                      'create_proc_entry'
scan "procfs file_operations/proc_ops"        'proc_create|struct proc_ops'
scan "access_ok("                             'access_ok\('
scan "set_fs/get_fs"                          'set_fs\(|get_fs\('
scan "old timers (setup_timer/init_timer/del_timer)" 'setup_timer|init_timer\b|del_timer'
scan "timer_setup"                            'timer_setup'
scan "y2038 time APIs"                        'do_gettimeofday|getnstimeofday|current_kernel_time|struct timeval'
scan "netdev ndo_/alloc_netdev"               'ndo_|alloc_netdev'
scan "vringh_"                                'vringh_'
scan "find_vqs"                               'find_vqs'
scan "dmaengine"                              'dma_async_|dmaengine'
scan "ACCESS_ONCE"                            'ACCESS_ONCE'
scan "legacy PM callbacks"                    'pci_set_power_state|\.suspend[[:space:]]*=|\.resume[[:space:]]*='
scan "kmap/kmap_atomic"                       'kmap_atomic|kmap\('
scan "kthread_*"                              'kthread_'
scan "strlcpy"                                'strlcpy'
scan "PDE_DATA"                               'PDE_DATA'
scan "request_firmware"                       'request_firmware'
