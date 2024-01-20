import React from "react";
import "./App.css";
import { DeviceRoute } from "./routes/device_route";
import { createBrowserRouter, RouterProvider } from "react-router-dom";
import { FileRoute } from "./routes/file_route";

function App() {
  const router = createBrowserRouter([
    { path: "/device/:ip", element: <DeviceRoute /> },
    { path: "/device/:ip/fs/:fname", element: <FileRoute /> },
  ]);

  return (
    <div className="App">
      <div className="mainbody">
        <RouterProvider router={router} />
      </div>
    </div>
  );
}

export default App;
