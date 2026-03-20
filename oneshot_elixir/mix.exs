defmodule OneshotElixir.MixProject do
  use Mix.Project

  def project do
    [
      apps_path: "apps",
      version: "0.1.0",
      elixir: "~> 1.19",
      start_permanent: Mix.env() == :prod,
      aliases: aliases(),
      releases: releases(),
      deps: deps()
    ]
  end

  # Dependencies listed here are available only for this
  # project and cannot be accessed from applications inside
  # the apps folder.
  #
  # Run "mix help deps" for examples and options.
  defp deps do
    []
  end

  defp aliases do
    [
      "oneshot.start": "run --no-start -e 'OneshotShell.CLI.main([])'",
      "oneshot.snapshot": "run --no-start -e 'OneshotShell.CLI.main([\"snapshot\"])'",
      "oneshot.diagnostics": "run --no-start -e 'OneshotShell.CLI.main([\"diagnostics\"])'"
    ]
  end

  defp releases do
    [
      oneshot: [
        applications: [
          oneshot_shell: :load,
          oneshot_core: :permanent,
          oneshot_ui: :load
        ]
      ]
    ]
  end
end
