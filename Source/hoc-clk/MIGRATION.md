** FOR DEVELOPERS UTILIZING SYSCLK API ONLY **

Ensure you include the latest hoc-clk ipc and header files in your project before proceeding

Before running migration replacements, change every reference to sys-clk's ramload api to this
ramLoad -> partLoad
SysClkRamLoad_All -> HocClkPartLoad_EMC
SysClkRamLoad_Cpu -> HocClkPartLoad_EMCCpu

API version reference must be changed. compare to HOCCLK_IPC_API_VERSION
If you use the service name, use HOCCLK_IPC_SERVICE_NAME

Remove checks for the u8 enabled in sysclk clockmanager struct. Check if hocclk is enabled by listening to IPC results

Run the following replace commands (case sensitive):

sysclk -> hocclk
SysClk -> HocClk
SYSCLK -> HOCCLK
sysClk -> hocClk

Your project is now migrated to run with HOC
