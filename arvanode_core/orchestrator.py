"""
arvanode_core.orchestrator - Provisioning & Flash Pipeline Orchestrator
ArvaCore Smart Irrigation Ecosystem (2026)

Coordinates the complete linear provisioning lifecycle:
Configuration -> Validation -> Header Injection -> Compilation -> Port Check -> Flashing -> Result
"""

from dataclasses import dataclass
from pathlib import Path
from typing import Optional, Dict, Any, List

from .models import NodeConfig
from .repository import ProfileRepository
from .build_service import BuildService, BuildResult
from .flash_service import FlashService, FlashResult


@dataclass
class ProvisioningResult:
    success: bool
    stage: str
    node_id: str
    port: Optional[str]
    message: str
    build_result: Optional[BuildResult] = None
    flash_result: Optional[FlashResult] = None
    dry_run: bool = False


class ProvisioningOrchestrator:
    """Thin workflow coordinator that chains compilation and flashing cleanly."""

    def __init__(
        self,
        repository: Optional[ProfileRepository] = None,
        build_service: Optional[BuildService] = None,
        flash_service: Optional[FlashService] = None
    ):
        self.repo = repository or ProfileRepository()
        self.builder = build_service or BuildService()
        self.flasher = flash_service or FlashService()

    def run_compile_only(self, config: NodeConfig, dry_run: bool = False) -> ProvisioningResult:
        """Executes compilation without attempting to upload to a serial port."""
        try:
            config.validate()
        except ValueError as ex:
            return ProvisioningResult(
                success=False,
                stage="VALIDATION",
                node_id=getattr(config, "logical_node_id", "UNKNOWN"),
                port=None,
                message=f"Configuration validation failed: {ex}",
                dry_run=dry_run
            )

        build_res = self.builder.compile(config, dry_run=dry_run)
        if not build_res.success:
            return ProvisioningResult(
                success=False,
                stage="COMPILATION",
                node_id=config.logical_node_id,
                port=None,
                message=f"Compilation failed: {build_res.error}",
                build_result=build_res,
                dry_run=dry_run
            )

        return ProvisioningResult(
            success=True,
            stage="COMPILATION",
            node_id=config.logical_node_id,
            port=None,
            message="Firmware compiled cleanly.",
            build_result=build_res,
            dry_run=dry_run
        )

    def run_full_provision(
        self,
        config: NodeConfig,
        port: str,
        baud: Optional[int] = None,
        dry_run: bool = False
    ) -> ProvisioningResult:
        """Runs validation, build, and flash sequentially with strict error halting."""
        # Stage 1: Validation
        try:
            config.validate()
        except ValueError as ex:
            return ProvisioningResult(
                success=False,
                stage="VALIDATION",
                node_id=getattr(config, "logical_node_id", "UNKNOWN"),
                port=port,
                message=f"Configuration validation failed: {ex}",
                dry_run=dry_run
            )

        # Stage 2: Compilation
        build_res = self.builder.compile(config, dry_run=dry_run)
        if not build_res.success:
            return ProvisioningResult(
                success=False,
                stage="COMPILATION",
                node_id=config.logical_node_id,
                port=port,
                message=f"Compilation failed during build stage: {build_res.error}",
                build_result=build_res,
                dry_run=dry_run
            )

        # Stage 3: Flashing
        flash_res = self.flasher.flash(
            sketch_dir=self.builder.sketch_dir,
            port=port,
            baud=baud,
            dry_run=dry_run
        )

        if not flash_res.success:
            return ProvisioningResult(
                success=False,
                stage="FLASHING",
                node_id=config.logical_node_id,
                port=port,
                message=f"Flashing failed: {flash_res.error}",
                build_result=build_res,
                flash_result=flash_res,
                dry_run=dry_run
            )

        return ProvisioningResult(
            success=True,
            stage="COMPLETED",
            node_id=config.logical_node_id,
            port=port,
            message="Device firmware successfully provisioned and flashed.",
            build_result=build_res,
            flash_result=flash_res,
            dry_run=dry_run
        )
