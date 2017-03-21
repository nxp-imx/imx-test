OBJS = $(OBJDIR)/autorun-mmc.sh $(OBJDIR)/autorun-mmc-blockrw.sh $(OBJDIR)/autorun-mmc-fdisk.sh $(OBJDIR)/autorun-mmc-mkfs.sh $(OBJDIR)/autorun-mmc-fs.sh

all : $(OBJS)
	mv $(OBJDIR)/autorun-mmc.sh $(OBJDIR)/MMC_SD_SDIO_Host
	mv $(OBJDIR)/autorun-mmc-blockrw.sh $(OBJDIR)/MMC_SD_SDIO_Host
	mv $(OBJDIR)/autorun-mmc-fdisk.sh $(OBJDIR)/MMC_SD_SDIO_Host
	mv $(OBJDIR)/autorun-mmc-mkfs.sh $(OBJDIR)/MMC_SD_SDIO_Host
	mv $(OBJDIR)/autorun-mmc-fs.sh $(OBJDIR)/MMC_SD_SDIO_Host

.PHONY: clean
clean :
	rm -f $(OBJS)

#
# include the Rules
#
include ../make.rules

