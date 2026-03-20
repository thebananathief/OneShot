defmodule OneshotShell.MixProject do
  use Mix.Project

  def project do
    [
      app: :oneshot_shell,
      version: "0.1.0",
      build_path: "../../_build",
      config_path: "../../config/config.exs",
      deps_path: "../../deps",
      lockfile: "../../mix.lock",
      elixir: "~> 1.19",
      start_permanent: Mix.env() == :prod,
      deps: deps()
    ]
  end

  # Run "mix help deps" to learn about dependencies.
  defp deps do
    [
      {:oneshot_core, in_umbrella: true},
      {:oneshot_ui, in_umbrella: true}
    ]
  end
end
