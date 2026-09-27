# Stage 65 — Passthrough belongs in Environment

The user confirmed that passthrough works on Stage 64. No additional renderer,
Wave lifecycle, pose or alpha changes are included in this stage.

- Move **Passthrough background** and **Start with Passthrough Mode** from
  **Settings > Display** to the top of **Settings > Environment**.
- Preserve the existing startup preference and immediate toggle behavior.
- Refresh the controls when the Environment panel is reopened, including after
  changes made from the three-dot menu. Simply opening the panel does not switch
  the active background.
- Selecting a virtual environment turns off passthrough so that the selected
  environment can be seen. This does not alter the next-launch preference.
- Reset Environment Settings resets the background controls too; Reset Display
  Settings no longer changes the passthrough startup preference.
- Both controls remain hidden on platforms without passthrough support.

The current-session switch and next-launch preference remain separate settings.
No preference-key migration, data clearing or GitHub publication is required.
