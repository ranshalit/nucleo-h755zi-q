/* Read-only speech models in flash bank 2 (programmed from zephyr.hex). */
.speech_model_bank2 :
{
	__speech_model_bank2_start = .;
	KEEP(*(.speech_model_bank2 .speech_model_bank2.*))
	__speech_model_bank2_end = .;
} > SPEECH_BANK2

/* Share the unused model-sector space with the read-only dashboard script. */
.web_assets_bank2 :
{
	KEEP(*(.web_assets_bank2))
} > SPEECH_BANK2
