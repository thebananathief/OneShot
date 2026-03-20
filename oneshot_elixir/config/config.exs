# This file is responsible for configuring your umbrella
# and **all applications** and their dependencies with the
# help of the Config module.
#
# Note that all applications in your umbrella share the
# same configuration and dependencies, which is why they
# all use the same configuration file. If you want different
# configurations or dependencies per app, it is best to
# move said applications out of the umbrella.
import Config

local_app_data =
  System.get_env("LOCALAPPDATA") ||
    Path.join([System.user_home!(), "AppData", "Local"])

pictures_dir =
  case System.get_env("USERPROFILE") do
    nil -> Path.join([System.user_home!(), "Pictures"])
    profile -> Path.join(profile, "Pictures")
  end

config :logger, :default_formatter, format: "$date $time [$level] $message\n"

config :oneshot_core,
  ipc_host: {127, 0, 0, 1},
  ipc_port: 45_731,
  lock_path: Path.join([local_app_data, "OneShot", "oneshot.lock"]),
  temp_dir: Path.join([local_app_data, "OneShot", "Temp"]),
  save_dir: Path.join([pictures_dir, "Screenshots"]),
  startup_key: "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Run",
  app_name: "OneShot"
